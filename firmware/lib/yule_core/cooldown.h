// Press cooldown and the mapping from button gestures to device actions. Hardware independent.
#pragma once

#include <cstdint>

#include "button_gestures.h"
#include "yule_limits.h"

namespace yule {

// After a successful send, short presses are ignored for a while. A failed send does not start it.
class Cooldown {
 public:
  explicit Cooldown(uint32_t durationMs = limits::kDefaultCooldownMs) : durationMs_(durationMs) {}

  void setDurationMs(uint32_t ms) { durationMs_ = ms; }
  uint32_t durationMs() const { return durationMs_; }

  void start(uint32_t nowMs) {
    started_ = true;
    startMs_ = nowMs;
  }
  bool active(uint32_t nowMs) const { return started_ && durationMs_ > 0 && nowMs - startMs_ < durationMs_; }
  uint32_t remainingMs(uint32_t nowMs) const { return active(nowMs) ? durationMs_ - (nowMs - startMs_) : 0; }

 private:
  uint32_t durationMs_;
  bool started_ = false;
  uint32_t startMs_ = 0;
};

enum class ControlAction : uint8_t {
  None,
  SendPress,        // short press: broadcast a press
  PleaseWait,       // short press during cooldown (or while a send is still in flight)
  ToggleMute,       // long press
  ShowLongCue,      // held for 2 s: show the long-press cue
  OpenWifiSetup,    // held for 10 s: re-open Wi-Fi setup (also show the very-long cue)
  ClearHoldCue,     // the button was released: remove any hold-progress cue
};

// Turns gesture events into actions. Only short presses are affected by the cooldown: long and very long
// presses always work.
class ControlLogic {
 public:
  explicit ControlLogic(uint32_t cooldownMs = limits::kDefaultCooldownMs) : cooldown_(cooldownMs) {}

  ControlAction onGesture(GestureEvent event, uint32_t nowMs);

  // Report the outcome of a SendPress. Success starts the cooldown; failure does not, so the owner can retry.
  void onSendResult(bool success, uint32_t nowMs);

  bool sendInFlight() const { return sendInFlight_; }
  Cooldown& cooldown() { return cooldown_; }
  const Cooldown& cooldown() const { return cooldown_; }

 private:
  Cooldown cooldown_;
  bool sendInFlight_ = false;
};

}  // namespace yule
