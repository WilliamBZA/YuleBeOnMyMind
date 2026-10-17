// Queue of presses waiting to be played and the timing of each playback session:
//   lights (and the song, capped at 30 s) for at least 10 s -> 1 s fade-out -> 1 s pause if another is waiting.
// At most 5 presses wait; further ones are dropped. Hardware independent.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>

#include "press_message.h"
#include "rtttl.h"

namespace yule {

struct PlaybackItem {
  PressMessage message;
  Melody melody;         // the parsed song
  bool preview = false;  // a local preview from the settings page: plays even when muted
};

class PlaybackScheduler {
 public:
  enum class Phase { Idle, Playing, Fading, Gap };

  // What changed during an update(); several can happen in one call if the caller was late.
  struct Events {
    bool started = false;      // a new item became current: start its song and pattern
    bool fadeStarted = false;  // the current item's lights should fade out now
    bool ended = false;        // the current item has finished
    bool any() const { return started || fadeStarted || ended; }
  };

  // Lights stay on for the song's played length, but for at least 10 s.
  static uint32_t lightsDurationMs(uint32_t songClippedMs);

  // Adds a press. If nothing is playing it starts at once (reported by the next update()). Returns false
  // and drops the press when 5 are already waiting.
  bool enqueue(PlaybackItem item, uint32_t nowMs);

  // Interrupts whatever is playing with this item (used for previews); waiting presses are kept.
  void playNow(PlaybackItem item, uint32_t nowMs);

  // Drops everything waiting and cancels the current playback.
  void clear();

  Events update(uint32_t nowMs);

  Phase phase() const { return phase_; }
  // The item whose song/lights are showing (Playing or Fading), otherwise nullptr.
  const PlaybackItem* current() const { return phase_ == Phase::Playing || phase_ == Phase::Fading ? &current_ : nullptr; }
  size_t waiting() const { return queue_.size(); }
  // True when nothing is playing, fading, pausing or waiting. Firmware updates install only when idle.
  bool idle() const { return phase_ == Phase::Idle && queue_.empty(); }
  // Milliseconds since the current item started (0 when not playing).
  uint32_t elapsedInItem(uint32_t nowMs) const { return phase_ == Phase::Playing ? nowMs - itemStartMs_ : 0; }

 private:
  void startItem(PlaybackItem item, uint32_t nowMs);

  std::deque<PlaybackItem> queue_;
  PlaybackItem current_;
  Phase phase_ = Phase::Idle;
  uint32_t itemStartMs_ = 0;   // when the current item started
  uint32_t phaseStartMs_ = 0;  // when the current phase started
  uint32_t lightsMs_ = 0;
  bool pendingStart_ = false;
};

}  // namespace yule
