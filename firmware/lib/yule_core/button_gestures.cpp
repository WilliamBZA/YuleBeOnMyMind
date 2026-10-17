#include "button_gestures.h"

namespace yule {

void ButtonGestures::push(GestureEvent e) {
  if (count_ >= kQueueSize) return;  // the app polls every tick; this never fills in practice
  queue_[(head_ + count_) % kQueueSize] = e;
  ++count_;
}

bool ButtonGestures::poll(GestureEvent& event) {
  if (count_ == 0) return false;
  event = queue_[head_];
  head_ = (head_ + 1) % kQueueSize;
  --count_;
  return true;
}

void ButtonGestures::update(uint32_t nowMs, bool pressed) {
  if (!initialised_) {
    // Start from "released". A button already down at the first sample counts as a press that began now.
    initialised_ = true;
    candidate_ = pressed;
    candidateSinceMs_ = nowMs;
    stablePressed_ = false;
    return;
  }

  if (pressed != candidate_) {
    candidate_ = pressed;
    candidateSinceMs_ = nowMs;
  }

  if (candidate_ != stablePressed_ && nowMs - candidateSinceMs_ >= limits::kDebounceMs) {
    stablePressed_ = candidate_;
    if (stablePressed_) {
      pressStartMs_ = candidateSinceMs_;
      longFired_ = false;
      veryLongFired_ = false;
    } else {
      const uint32_t heldMs = candidateSinceMs_ - pressStartMs_;
      // The release is only confirmed after the debounce time, so a hold that ended right at a threshold
      // may not have been observed crossing it while the button was down. Report the thresholds it passed.
      if (!longFired_ && heldMs >= limits::kLongPressAtMs) {
        longFired_ = true;
        push(GestureEvent::LongThresholdReached);
      }
      if (!veryLongFired_ && heldMs >= limits::kVeryLongPressAtMs) {
        veryLongFired_ = true;
        push(GestureEvent::VeryLongThresholdReached);
      }
      if (!veryLongFired_) {
        if (heldMs < limits::kShortPressBelowMs) {
          push(GestureEvent::ShortPress);
        } else if (heldMs >= limits::kLongPressAtMs) {
          push(GestureEvent::LongPress);
        }  // 1 s to 2 s: nothing
      }
      push(GestureEvent::Released);
    }
  }

  // Thresholds are judged while the button is really down. Once the raw level has gone back up the hold is
  // over, even though the release is only confirmed after the debounce time.
  if (stablePressed_ && candidate_) {
    const uint32_t heldMs = nowMs - pressStartMs_;
    if (!longFired_ && heldMs >= limits::kLongPressAtMs) {
      longFired_ = true;
      push(GestureEvent::LongThresholdReached);
    }
    if (!veryLongFired_ && heldMs >= limits::kVeryLongPressAtMs) {
      veryLongFired_ = true;
      push(GestureEvent::VeryLongThresholdReached);
    }
  }
}

}  // namespace yule
