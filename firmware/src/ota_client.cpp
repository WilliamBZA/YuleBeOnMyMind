#include "ota_client.h"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include <memory>
#include <new>

#include "log.h"
#include "yule_config.h"

// The Arduino core marks a freshly installed image valid on its own at start-up unless this returns true.
// It is a C function in the core (esp32-hal-misc.c) with a weak default.
extern "C" bool verifyRollbackLater() { return true; }

namespace {

constexpr uint32_t kTaskStackBytes = 12288;  // the TLS handshake and ECDSA verification run on this task
constexpr int kHttpTimeoutMs = 15000;
constexpr size_t kChunkBytes = 4096;
constexpr size_t kMaxSignatureBytes = 80;    // a DER ECDSA P-256 signature is at most 72 bytes

const char* const kReleaseKeys[] = {YULE_RELEASE_PUBKEY_1, YULE_RELEASE_PUBKEY_2};

std::string hex(const uint8_t* bytes, size_t n) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) {
    out.push_back(digits[bytes[i] >> 4]);
    out.push_back(digits[bytes[i] & 0x0f]);
  }
  return out;
}

// Parses a built-in PEM key; false for an empty or placeholder key.
bool loadKey(const char* pem, mbedtls_pk_context& pk) {
  const size_t len = strlen(pem);
  if (len == 0) return false;
  mbedtls_pk_init(&pk);
  // The length includes the terminating NUL, as mbedTLS requires for PEM input.
  if (mbedtls_pk_parse_public_key(&pk, reinterpret_cast<const unsigned char*>(pem), len + 1) != 0 ||
      !mbedtls_pk_can_do(&pk, MBEDTLS_PK_ECKEY)) {
    mbedtls_pk_free(&pk);
    return false;
  }
  return true;
}

// True when the DER signature over `digest` verifies against one of the built-in release keys.
bool signatureVerifies(const uint8_t digest[32], const std::string& signatureBase64, std::string& error) {
  uint8_t sig[kMaxSignatureBytes];
  size_t sigLen = 0;
  if (mbedtls_base64_decode(sig, sizeof(sig), &sigLen, reinterpret_cast<const unsigned char*>(signatureBase64.data()),
                            signatureBase64.size()) != 0 ||
      sigLen == 0) {
    error = "the signature in the manifest is not valid base64";
    return false;
  }
  int usable = 0;
  for (const char* pem : kReleaseKeys) {
    mbedtls_pk_context pk;
    if (!loadKey(pem, pk)) continue;
    ++usable;
    const int rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, 32, sig, sigLen);
    mbedtls_pk_free(&pk);
    if (rc == 0) return true;
  }
  error = usable == 0 ? "this firmware has no usable release public key" : "the signature does not match the release keys";
  return false;
}

}  // namespace

int OtaClient::usableReleaseKeys() {
  int usable = 0;
  for (const char* pem : kReleaseKeys) {
    mbedtls_pk_context pk;
    if (!loadKey(pem, pk)) continue;
    mbedtls_pk_free(&pk);
    ++usable;
  }
  return usable;
}

bool OtaClient::start(const yule::OtaManifest& manifest) {
  if (busy()) return false;
  manifest_ = manifest;
  failure_.clear();
  percent_.store(0);
  phase_.store(Phase::Downloading);
  if (xTaskCreate(&OtaClient::taskEntry, "yule-ota", kTaskStackBytes, this, 1, nullptr) != pdPASS) {
    failure_ = "could not start the update task (out of memory)";
    phase_.store(Phase::Failed);
    return false;
  }
  return true;
}

bool OtaClient::takeFailure(std::string& reason) {
  if (phase() != Phase::Failed) return false;
  reason = failure_;
  phase_.store(Phase::Idle);
  return true;
}

void OtaClient::taskEntry(void* arg) {
  static_cast<OtaClient*>(arg)->run();
  vTaskDelete(nullptr);
}

void OtaClient::run() {
  std::string error;
  if (install(error)) {
    phase_.store(Phase::ReadyToRestart);
  } else {
    failure_ = error;
    phase_.store(Phase::Failed);  // published last: the main loop reads failure_ only after seeing Failed
  }
}

bool OtaClient::install(std::string& error) {
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  if (!target) {
    error = "there is no app slot to install into";
    return false;
  }
  if (manifest_.size == 0 || manifest_.size > target->size) {
    error = "the image does not fit in an app slot";
    return false;
  }

  esp_http_client_config_t cfg = {};
  cfg.url = manifest_.url.c_str();
  cfg.crt_bundle_attach = esp_crt_bundle_attach;  // verifies the server's certificate and host name
  cfg.timeout_ms = kHttpTimeoutMs;
  cfg.keep_alive_enable = false;
  esp_http_client_handle_t http = esp_http_client_init(&cfg);
  if (!http) {
    error = "could not create the HTTPS client (out of memory)";
    return false;
  }
  std::unique_ptr<esp_http_client, decltype(&esp_http_client_cleanup)> httpGuard(http, &esp_http_client_cleanup);

  if (esp_http_client_open(http, 0) != ESP_OK) {
    error = "could not connect to the download server (network or certificate problem)";
    return false;
  }
  const int64_t length = esp_http_client_fetch_headers(http);
  const int status = esp_http_client_get_status_code(http);
  if (status != 200) {
    error = "the download server answered HTTP " + std::to_string(status);
    esp_http_client_close(http);
    return false;
  }
  if (length > 0 && static_cast<uint64_t>(length) != manifest_.size) {
    error = "the download is not the size the manifest says";
    esp_http_client_close(http);
    return false;
  }

  esp_ota_handle_t ota = 0;
  if (esp_ota_begin(target, manifest_.size, &ota) != ESP_OK) {
    error = "could not prepare the app slot";
    esp_http_client_close(http);
    return false;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);

  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[kChunkBytes]);
  bool ok = buf != nullptr;
  if (!ok) error = "out of memory";
  uint32_t received = 0;
  while (ok) {
    const int n = esp_http_client_read(http, reinterpret_cast<char*>(buf.get()), kChunkBytes);
    if (n < 0) {
      error = "the download was interrupted";
      ok = false;
      break;
    }
    if (n == 0) break;  // end of the body (or the connection closed early: checked below)
    if (received + static_cast<uint32_t>(n) > manifest_.size) {
      error = "the download is larger than the manifest says";
      ok = false;
      break;
    }
    mbedtls_sha256_update(&sha, buf.get(), static_cast<size_t>(n));
    const esp_err_t w = esp_ota_write(ota, buf.get(), static_cast<size_t>(n));
    if (w != ESP_OK) {
      error = w == ESP_ERR_OTA_VALIDATE_FAILED ? "the download is not an ESP32 firmware image" : "writing to flash failed";
      ok = false;
      break;
    }
    received += static_cast<uint32_t>(n);
    percent_.store(static_cast<int>(static_cast<uint64_t>(received) * 100 / manifest_.size));
  }
  esp_http_client_close(http);

  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);

  if (ok && received != manifest_.size) {
    error = "the download ended early";
    ok = false;
  }
  if (ok) {
    phase_.store(Phase::Verifying);
    if (hex(digest, sizeof(digest)) != manifest_.sha256) {
      error = "the image's SHA-256 does not match the manifest";
      ok = false;
    }
  }
  if (ok) ok = signatureVerifies(digest, manifest_.signature, error);
  if (!ok) {
    esp_ota_abort(ota);
    return false;
  }

  // esp_ota_end also checks the image's own structure and checksums.
  if (esp_ota_end(ota) != ESP_OK) {
    error = "the image failed the ESP32's own image check";
    return false;
  }
  if (esp_ota_set_boot_partition(target) != ESP_OK) {
    error = "could not switch to the new firmware";
    return false;
  }
  return true;
}

void otaConfirmRunningImage() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
  }
}

void otaRevertToPreviousFirmware(std::string& error) {
  const esp_partition_t* previous = esp_ota_get_next_update_partition(nullptr);  // the other of the two slots
  esp_app_desc_t desc;
  if (!previous || esp_ota_get_partition_description(previous, &desc) != ESP_OK) {
    error = "the other app slot holds no firmware to go back to";
    return;
  }
  if (esp_ota_set_boot_partition(previous) != ESP_OK) {
    error = "the firmware in the other app slot is not valid";
    return;
  }
  yuleLog("ota", "restarting into the previous firmware in app slot %s", previous->label);
  delay(200);  // let the log line out
  esp_restart();
}
