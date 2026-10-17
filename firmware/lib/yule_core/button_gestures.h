// Button gesture recogniser: debounce, then classify by hold time. A pure state machine driven by
// (nowMs, pressed), so it is tested on a PC. Hardware independent.
//
//   hold < 1 s          -> ShortPress      (on release)
//   1 s <= hold < 2 s   -> nothing
//   2 s <= hold < 10 s  -> LongPress       (on release); LongThresholdReached fires when 2 s is reached
//   hold >= 10 s        -> VeryLongThresholdReached fires when 10 s is reached; nothing on release
#pragma once

#include <cstdint>

#include "yule_limits.h"

namespace yule {

enum class GestureEvent : uint8_t {
  None,
  ShortPress,
  LongPress,
  LongThresholdReached,      // held for 2 s: releasing now will be a long press
  VeryLongThresholdReached,  // held for 10 s: the very-long-press action happens now
  Released,                  // the button was let go (after any ShortPress/LongPress for that hold)
};

class ButtonGestures {
 public:
  // `pressed` is true while the button is physically down (the driver inverts the pull-up level).
  // Call every loop tick with a monotonic millisecond clock.
  void update(uint32_t nowMs, bool pressed);

  // Pops the next pending event; returns false when there is none.
  bool poll(GestureEvent& event);

  // True from the moment a press is confirmed (after debouncing) until its release is confirmed.
  bool held() const { return stablePressed_; }

 private:
  void push(GestureEvent e);

  static constexpr int kQueueSize = 6;
  GestureEvent queue_[kQueueSize] = {};
  int head_ = 0;
  int count_ = 0;

  bool initialised_ = false;
  bool candidate_ = false;        // most recent raw level
  uint32_t candidateSinceMs_ = 0; // when the raw level last changed
  bool stablePressed_ = false;    // debounced level
  uint32_t pressStartMs_ = 0;
  bool longFired_ = false;
  bool veryLongFired_ = false;
};

}  // namespace yule
