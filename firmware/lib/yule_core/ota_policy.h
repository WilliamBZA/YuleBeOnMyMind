// Firmware-update decisions: whether to install a published release, and the rollback state machine that
// reverts a bad update. Hardware independent; src/ota_client and src/storage carry out the actions.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ota_manifest.h"
#include "yule_limits.h"

namespace yule {

// ---- Install decision ----

enum class InstallDecision {
  Install,
  SkipBoardMismatch,  // the release is for another board type
  SkipBadVersion,     // the running or published version is not MAJOR.MINOR.PATCH
  SkipNotNewer,       // the same or an older version
  SkipRolledBack,     // we rolled back from this exact version before: never retry it
  SkipNotIdle,        // playing, previewing, in Wi-Fi setup, or the button is held: try again later
};

const char* installDecisionName(InstallDecision d);

struct InstallContext {
  std::string board;
  std::string runningVersion;
  std::vector<std::string> rolledBackVersions;
  bool idle = true;
};

// Decides whether to start installing `manifest` now. The signature and digest are checked later, on the
// downloaded image; this decides only whether to download at all.
InstallDecision decideInstall(const OtaManifest& manifest, const InstallContext& context);

// ---- Result of the most recent update attempt (reported in status and on the settings page) ----

struct OtaResult {
  enum class Kind { None, Ok, Failed, RolledBack };
  Kind kind = Kind::None;
  std::string detail;  // failure reason, or the version rolled back from

  // "none", "ok", "failed:<reason>" or "rolled-back:<version>"
  std::string toString() const;
  static OtaResult fromString(std::string_view text);
  bool operator==(const OtaResult& o) const { return kind == o.kind && detail == o.detail; }
};

// ---- Persistent update state (NVS namespace `ota`) and the rollback state machine ----

enum class BootAction { Continue, Revert };

struct OtaState {
  bool provisional = false;               // running a freshly installed image that has not proved itself
  uint8_t bootAttempts = 0;               // boots of the provisional image so far
  std::string installedVersion;           // the version the provisional image should be (empty if unknown)
  std::vector<std::string> rolledBack;    // versions we rolled back from, oldest first, at most 4
  OtaResult lastResult;

  bool isRolledBack(std::string_view version) const;

  // The installer calls this just before rebooting into the new image.
  void markInstalled(const std::string& version = "");

  // Called once, early, on every boot. While provisional it counts the boot; after 3 unhealthy boots the
  // image is recorded as rolled back and Revert is returned: switch to the previous slot and reboot.
  // If a different version than the one installed is running, the new image never got this far (the
  // bootloader went back to the previous one): it is recorded as rolled back and the boot continues.
  BootAction onBoot(const std::string& runningVersion);

  // The provisional image has proved itself.
  void markHealthy();

  // Records that we are abandoning `version` (called by onBoot, and by the caller on a health-based revert).
  void recordRollback(const std::string& version);

  // A download or verification failure that left the running firmware untouched.
  void recordFailure(const std::string& reason);

  std::string toJson() const;
  static OtaState fromJson(std::string_view json);
};

// Decides when a provisional image counts as healthy, or when it should be reverted. Times are
// milliseconds since boot. Healthy means 60 s of uptime and, whenever Wi-Fi is available, a broker
// connection. A provisional image that has had Wi-Fi for 10 minutes without ever reaching the broker is
// reverted.
class HealthMonitor {
 public:
  enum class Verdict { Pending, Healthy, Revert };

  void onWifi(bool up, uint32_t nowMs);
  void onBrokerConnected();
  Verdict update(uint32_t nowMs);

 private:
  bool wifiEverUp_ = false;
  bool wifiUp_ = false;
  uint32_t wifiUpSinceMs_ = 0;
  uint32_t wifiUpAccumMs_ = 0;  // Wi-Fi-up time from earlier up periods
  bool brokerConnected_ = false;
};

}  // namespace yule
