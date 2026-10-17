// Decides what the Wi-Fi layer should do next (device-provisioning): when to open the setup portal, when to
// retry the saved network and how long to wait between retries. A pure state machine driven by
// (nowMs, connected, hasSavedCredentials), so it is tested on a PC. Hardware independent; the ESP32 glue
// (src/wifi_manager_glue) carries out the Actions it returns.
//
//   boot, no saved network  -> Portal (FirstRun): stays open until a network is configured
//   boot, saved network     -> Attempting; failures back off 1 s, 2 s, 4 s ... capped at 60 s. The portal
//                              is never opened automatically, however long the network stays away.
//   very long press         -> Portal (OnDemand): closes after 10 minutes and returns to the saved network
//   a network joined        -> Connected (the portal, if any, is closed)
#pragma once

#include <cstdint>

#include "backoff.h"
#include "yule_limits.h"

namespace yule {

class WifiSupervisor {
 public:
  enum class State : uint8_t {
    Attempting,  // a connection attempt to the saved network is under way
    BackingOff,  // waiting before the next attempt
    Connected,
    Portal,      // the setup access point is open
  };

  enum class PortalReason : uint8_t {
    None,
    FirstRun,  // no network is saved: stays open until one is configured
    OnDemand,  // the owner asked for it: closes by itself after the portal timeout
  };

  // What the glue must do now. Several can be set at once; apply them in the order closePortal, openPortal,
  // beginConnect.
  struct Actions {
    bool beginConnect = false;  // start (or restart) connecting to the saved network
    bool openPortal = false;    // start the setup access point and its captive page
    bool closePortal = false;   // stop the setup access point
    bool any() const { return beginConnect || openPortal || closePortal; }
  };

  struct Config {
    uint32_t attemptTimeoutMs = 20000;                 // give up on one attempt after this long
    uint32_t portalTimeoutMs = 10UL * 60UL * 1000UL;   // an on-demand portal closes after this long
    uint32_t portalSettleMs = 1500;                    // ignore "connected" this long after opening the portal
    uint32_t backoffBaseMs = 1000;
    uint32_t backoffCapMs = limits::kBackoffCapMs;
    uint32_t seed = 1;                                 // jitter seed
  };

  WifiSupervisor();
  explicit WifiSupervisor(const Config& config);

  // Call once at boot.
  Actions start(uint32_t nowMs, bool hasSavedCredentials);

  // Call every loop tick. `connected` is true while the station has an IP address. `hasSavedCredentials`
  // means there is a network to go back to (including one the glue is holding aside during an on-demand
  // portal).
  Actions update(uint32_t nowMs, bool connected, bool hasSavedCredentials);

  // The very long press. Ignored while the portal is already open.
  Actions requestPortal(uint32_t nowMs);

  State state() const { return state_; }
  bool connected() const { return state_ == State::Connected; }
  bool portalActive() const { return state_ == State::Portal; }
  PortalReason portalReason() const { return portalReason_; }

  // Milliseconds the station has been connected, 0 when it is not.
  uint32_t connectedForMs(uint32_t nowMs) const { return connected() ? nowMs - connectedSinceMs_ : 0; }

  // Connection attempts started since boot, for the serial log.
  uint32_t attemptsStarted() const { return attemptsStarted_; }
  // The delay chosen for the current back-off wait (valid in BackingOff).
  uint32_t currentBackoffMs() const { return currentBackoffMs_; }

 private:
  void enterAttempting(uint32_t nowMs, Actions& a);
  void enterBackingOff(uint32_t nowMs);
  void enterConnected(uint32_t nowMs);
  void enterPortal(uint32_t nowMs, PortalReason reason, Actions& a);

  Config cfg_;
  Backoff backoff_;
  State state_ = State::Attempting;
  PortalReason portalReason_ = PortalReason::None;
  uint32_t stateSinceMs_ = 0;
  uint32_t retryAtMs_ = 0;
  uint32_t connectedSinceMs_ = 0;
  uint32_t attemptsStarted_ = 0;
  uint32_t currentBackoffMs_ = 0;
};

}  // namespace yule
