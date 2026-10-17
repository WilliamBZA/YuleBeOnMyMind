// Feedback cues (overlay layer) and status indications (base layer, shown while idle), per design D8.
// Timings and colours are first guesses, to be tuned on hardware. Hardware independent.
#pragma once

#include <cstdint>
#include <memory>

#include "animation.h"

namespace yule {

enum class Cue : uint8_t {
  Sent,               // 2 green pulses
  SendFailed,         // 3 red blinks
  PleaseWait,         // 1 dim amber pulse
  LongPressReached,   // steady blue while the button is held past 2 s
  VeryLongPressReached,  // purple chase while held past 10 s
  Muted,              // blue fade-down
  Unmuted,            // blue fade-up
};

// How long a cue lasts. 0 means it stays until cleared (the two hold-progress cues).
uint32_t cueDurationMs(Cue cue);

std::shared_ptr<Animation> makeCue(Cue cue);

// Status indications. Idle (connected, nothing playing) is simply no base animation: all LEDs off.
std::shared_ptr<Animation> makeSetupIndication();           // slow blue breathing: the Wi-Fi portal is open
std::shared_ptr<Animation> makeConnectingIndication();      // slow amber pulse while connecting after boot
std::shared_ptr<Animation> makeConnectionLostIndication();  // one dim red LED blinking about every 5 s

// A dim white bar along the strip that fills in proportion to the download/install percentage.
class UpdateProgressIndication : public Animation {
 public:
  void setPercent(int percent);
  int percent() const { return percent_; }
  void render(uint32_t tMs, Rgb* px, size_t n) override;

 private:
  int percent_ = 0;
};

}  // namespace yule
