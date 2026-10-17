// Non-blocking melody playback: a pure state machine that, given the current time, says which frequency
// (if any) the buzzer should sound. The driver turns that into a square wave. Hardware independent.
#pragma once

#include <cstddef>
#include <cstdint>

#include "rtttl.h"
#include "yule_limits.h"

namespace yule {

class MelodyPlayer {
 public:
  // Starts a melody at nowMs, replacing any melody already playing. An empty melody finishes immediately.
  void start(Melody melody, uint32_t nowMs);

  // Stops at once; update() returns 0 afterwards.
  void stop();

  bool playing() const { return playing_; }

  // Frequency to sound at nowMs in Hz, or 0 for silence (rests, the gap between notes, and after the end).
  // Call regularly (every loop tick); it is cheap. Safe across millis() wrap-around.
  // Playback keeps advancing even if the caller chooses not to sound the result (mute), so the visual
  // timeline is unaffected by muting.
  uint16_t update(uint32_t nowMs);

  // Full length of the melody and the length actually played (capped at 30 s).
  uint32_t totalMs() const { return totalMs_; }
  uint32_t clippedMs() const { return clippedMs_; }

  // The short silence at the end of a note that separates repeated notes.
  static uint32_t articulationGapMs(uint32_t noteMs);

 private:
  Melody melody_;
  bool playing_ = false;
  uint32_t startMs_ = 0;
  uint32_t totalMs_ = 0;
  uint32_t clippedMs_ = 0;
  size_t index_ = 0;
  uint32_t noteStartOffsetMs_ = 0;
};

}  // namespace yule
