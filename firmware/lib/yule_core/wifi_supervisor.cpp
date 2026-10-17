#include "wifi_supervisor.h"

namespace yule {

namespace {
// True once `nowMs` has reached `deadlineMs`, correct across the 32-bit millisecond wrap (every ~49 days).
bool reached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}
}  // namespace

WifiSupervisor::WifiSupervisor() : WifiSupervisor(Config()) {}

WifiSupervisor::WifiSupervisor(const Config& config)
    : cfg_(config), backoff_(config.backoffBaseMs, config.backoffCapMs, config.seed) {}

void WifiSupervisor::enterAttempting(uint32_t nowMs, Actions& a) {
  state_ = State::Attempting;
  portalReason_ = PortalReason::None;
  stateSinceMs_ = nowMs;
  ++attemptsStarted_;
  a.beginConnect = true;
}

void WifiSupervisor::enterBackingOff(uint32_t nowMs) {
  state_ = State::BackingOff;
  stateSinceMs_ = nowMs;
  currentBackoffMs_ = backoff_.nextDelayMs();
  retryAtMs_ = nowMs + currentBackoffMs_;
}

void WifiSupervisor::enterConnected(uint32_t nowMs) {
  state_ = State::Connected;
  portalReason_ = PortalReason::None;
  stateSinceMs_ = nowMs;
  connectedSinceMs_ = nowMs;
  backoff_.reset();
}

void WifiSupervisor::enterPortal(uint32_t nowMs, PortalReason reason, Actions& a) {
  state_ = State::Portal;
  portalReason_ = reason;
  stateSinceMs_ = nowMs;
  a.openPortal = true;
}

WifiSupervisor::Actions WifiSupervisor::start(uint32_t nowMs, bool hasSavedCredentials) {
  Actions a;
  backoff_.reset();
  if (hasSavedCredentials) {
    enterAttempting(nowMs, a);
  } else {
    enterPortal(nowMs, PortalReason::FirstRun, a);
  }
  return a;
}

WifiSupervisor::Actions WifiSupervisor::requestPortal(uint32_t nowMs) {
  Actions a;
  if (state_ == State::Portal) return a;  // already open; do not restart its timeout
  enterPortal(nowMs, PortalReason::OnDemand, a);
  return a;
}

WifiSupervisor::Actions WifiSupervisor::update(uint32_t nowMs, bool connected, bool hasSavedCredentials) {
  Actions a;
  switch (state_) {
    case State::Attempting:
      if (connected) {
        enterConnected(nowMs);
      } else if (nowMs - stateSinceMs_ >= cfg_.attemptTimeoutMs) {
        enterBackingOff(nowMs);
      }
      break;

    case State::BackingOff:
      if (connected) {
        enterConnected(nowMs);  // the driver got there by itself
      } else if (!hasSavedCredentials) {
        enterPortal(nowMs, PortalReason::FirstRun, a);  // nothing left to retry
      } else if (reached(nowMs, retryAtMs_)) {
        enterAttempting(nowMs, a);
      }
      break;

    case State::Connected:
      if (!connected) enterBackingOff(nowMs);
      break;

    case State::Portal: {
      const uint32_t open = nowMs - stateSinceMs_;
      if (connected && open >= cfg_.portalSettleMs) {
        enterConnected(nowMs);
        a.closePortal = true;
      } else if (portalReason_ == PortalReason::OnDemand && open >= cfg_.portalTimeoutMs) {
        if (hasSavedCredentials) {
          // Nobody configured a network: close the access point and go back to the one we had.
          a.closePortal = true;
          enterAttempting(nowMs, a);
        } else {
          // Nothing to go back to, so keep the portal open as a first-run portal.
          portalReason_ = PortalReason::FirstRun;
        }
      }
      break;
    }
  }
  return a;
}

}  // namespace yule
