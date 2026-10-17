// The firmware manifest the server publishes (retained) on <root>/firmware/<channel>/<board>:
//   {"v":1,"version":"0.3.0","board":"esp32dev","size":1534016,"sha256":"<hex>","sig":"<base64 DER ECDSA P-256>",
//    "url":"https://yule.williamb.net/firmware/yule-0.3.0-esp32dev.bin"}
// Hardware independent: signature verification itself lives in src/ota_client (mbedTLS).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace yule {

struct SemVer {
  int major = 0;
  int minor = 0;
  int patch = 0;
};

// "MAJOR.MINOR.PATCH", each a non-negative number of at most 4 digits. Nothing else is accepted.
bool parseSemVer(std::string_view text, SemVer& out);
// <0, 0, >0 as a is older than, equal to, or newer than b.
int compareSemVer(const SemVer& a, const SemVer& b);

struct OtaManifest {
  int v = 1;
  std::string version;
  std::string board;
  uint32_t size = 0;
  std::string sha256;     // 64 lower-case hex digits
  std::string signature;  // base64 of the DER-encoded ECDSA signature
  std::string url;        // https:// only
};

struct ManifestResult {
  bool ok = false;
  OtaManifest manifest;
  std::string error;  // why it was rejected, for the serial log and the status page
};

// Decodes and validates a manifest. Unknown fields are ignored. Anything missing or malformed, including a
// non-HTTPS URL, rejects the whole manifest.
ManifestResult decodeManifest(std::string_view payload);

}  // namespace yule
