// Downloads and installs a firmware release in its own FreeRTOS task (design D3, D13), so the main loop
// keeps rendering the progress indication and handling the button meanwhile.
//
//  - HTTPS with the ESP-IDF certificate bundle and hostname verification.
//  - The image is streamed into the inactive app slot and hashed (SHA-256) as it arrives.
//  - The size and digest must match the manifest, and the manifest's ECDSA P-256 signature over the image
//    must verify against one of the release public keys built into this firmware (secrets.h).
//  - Only then is the boot partition switched. Any failure leaves the running firmware untouched.
//
// Whether to install at all is decided by the core's ota_policy; restarting into the new image and the
// rollback bookkeeping are done by App on the main loop.
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <string>

#include "ota_manifest.h"

class OtaClient {
 public:
  enum class Phase : uint8_t {
    Idle,
    Downloading,  // percent() is valid
    Verifying,
    ReadyToRestart,  // verified and the boot partition switched: restart now
    Failed,          // failure() says why; the running firmware is untouched
  };

  // Starts installing `manifest` in the background. Returns false if an install is already running.
  bool start(const yule::OtaManifest& manifest);

  Phase phase() const { return phase_.load(); }
  bool busy() const { return phase() == Phase::Downloading || phase() == Phase::Verifying || phase() == Phase::ReadyToRestart; }
  int percent() const { return percent_.load(); }
  const std::string& version() const { return manifest_.version; }

  // After Failed: the reason, once; the client then returns to Idle.
  bool takeFailure(std::string& reason);

  // How many of the built-in release public keys parse. 0 means no update can ever verify.
  static int usableReleaseKeys();

 private:
  static void taskEntry(void* arg);
  void run();
  bool install(std::string& error);

  yule::OtaManifest manifest_;
  std::string failure_;
  std::atomic<Phase> phase_{Phase::Idle};
  std::atomic<int> percent_{0};
};

// Called once at boot, before anything else touches the network. The ESP-IDF bootloader starts a freshly
// installed image as "pending verify" and would go back to the previous one at the next restart. This
// firmware counts its own boot attempts instead (ota_policy, three tries), so the image is confirmed as
// soon as that count is safely in NVS.
void otaConfirmRunningImage();

// Makes the other app slot the boot partition and restarts into it. Returns only on failure, with the
// reason (for example when the other slot holds no valid firmware).
void otaRevertToPreviousFirmware(std::string& error);
