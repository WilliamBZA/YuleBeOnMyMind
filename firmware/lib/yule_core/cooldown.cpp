#include "cooldown.h"

namespace yule {

ControlAction ControlLogic::onGesture(GestureEvent event, uint32_t nowMs) {
  switch (event) {
    case GestureEvent::ShortPress:
      if (sendInFlight_ || cooldown_.active(nowMs)) return ControlAction::PleaseWait;
      sendInFlight_ = true;
      return ControlAction::SendPress;
    case GestureEvent::LongPress: return ControlAction::ToggleMute;
    case GestureEvent::LongThresholdReached: return ControlAction::ShowLongCue;
    case GestureEvent::VeryLongThresholdReached: return ControlAction::OpenWifiSetup;
    case GestureEvent::Released: return ControlAction::ClearHoldCue;
    case GestureEvent::None: break;
  }
  return ControlAction::None;
}

void ControlLogic::onSendResult(bool success, uint32_t nowMs) {
  sendInFlight_ = false;
  if (success) cooldown_.start(nowMs);
}

}  // namespace yule
