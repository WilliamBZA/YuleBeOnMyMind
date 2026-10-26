// JSON bodies of the device's settings API (design D7) that are not plain profile or validation documents:
// GET /api/library and GET /api/status. Hardware independent; src/web_server fills in the live values.
// Nothing here ever carries the broker password.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace yule {

// GET /api/library: the built-in songs (with how long each plays), the effects and the colours each uses,
// the time-zone menu and the limits the page enforces before saving.
std::string encodeLibrary();

// Where a firmware update stands, for the settings page (firmware-updates, "Update progress and status").
struct UpdateView {
  enum class State {
    UpToDate,     // nothing installed since the last USB flash, or nothing newer published
    Waiting,      // a newer release is available and installs once the device is idle
    Downloading,  // percent is valid
    Verifying,    // checking the digest and signature
    Restarting,   // verified and switched over; restarting into the new firmware
    Checking,     // running a just-installed version that has not yet proved healthy
    Installed,    // the last update installed and proved healthy
    Failed,       // detail: the reason; the device kept its firmware
    RolledBack,   // detail: the version that was rolled back
  };
  State state = State::UpToDate;
  int percent = 0;
  std::string version;  // the release being installed or waited for
  std::string detail;
};

const char* updateStateName(UpdateView::State state);

struct StatusView {
  std::string deviceId;
  std::string firmwareVersion;
  std::string board;
  std::vector<std::string> groupNames;  // empty: not in any family group yet
  std::string channel;
  std::string wifiSsid;
  std::string ip;
  std::string hostname;  // yule-<suffix>.local
  std::string broker;    // "connected", "connecting", "waiting for the clock", "no Wi-Fi", "setup required", ...
  bool brokerConnected = false;
  std::string brokerHost;  // the host and port in use (override or build default)
  uint16_t brokerPort = 0;
  std::string brokerOverrideHost;  // the owner's override, empty for the default
  uint16_t brokerOverridePort = 0; // the owner's override, 0 for the default
  bool setupRequired = false;  // no broker password has been entered
  bool timeSynced = false;
  std::string localTime;       // "Sat 2026-10-17 15:42" when synced
  bool muted = false;
  bool quietNow = false;       // quiet hours are silencing the device right now
  uint32_t uptimeS = 0;
  UpdateView update;
};

std::string encodeStatus(const StatusView& status);

}  // namespace yule
