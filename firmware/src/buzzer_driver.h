// The passive piezo buzzer: an LEDC square wave at 50% duty (design D9). The core's MelodyPlayer decides
// which frequency to sound; this only turns it into a tone, or silence.
#pragma once

#include <Arduino.h>

class BuzzerDriver {
 public:
  void begin();

  // Sounds `hz`, or silences the buzzer for 0. Cheap to call every tick: the hardware is only touched when
  // the frequency changes.
  void play(uint16_t hz);

  void silence() { play(0); }

  uint16_t frequency() const { return current_; }

 private:
  bool attached_ = false;
  uint16_t current_ = 0;
};
