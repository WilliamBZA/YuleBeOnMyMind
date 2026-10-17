#include "playback_queue.h"

#include <utility>

#include "yule_limits.h"

namespace yule {

uint32_t PlaybackScheduler::lightsDurationMs(uint32_t songClippedMs) {
  const uint32_t song = songClippedMs < limits::kMelodyMaxMs ? songClippedMs : limits::kMelodyMaxMs;
  return song > limits::kMinLightsMs ? song : limits::kMinLightsMs;
}

void PlaybackScheduler::startItem(PlaybackItem item, uint32_t nowMs) {
  current_ = std::move(item);
  const uint32_t total = current_.melody.totalMs();
  lightsMs_ = lightsDurationMs(total);
  phase_ = Phase::Playing;
  itemStartMs_ = nowMs;
  phaseStartMs_ = nowMs;
  pendingStart_ = true;
}

bool PlaybackScheduler::enqueue(PlaybackItem item, uint32_t nowMs) {
  if (phase_ == Phase::Idle && queue_.empty()) {
    startItem(std::move(item), nowMs);
    return true;
  }
  if (queue_.size() >= limits::kQueueCapacity) return false;
  queue_.push_back(std::move(item));
  return true;
}

void PlaybackScheduler::playNow(PlaybackItem item, uint32_t nowMs) { startItem(std::move(item), nowMs); }

void PlaybackScheduler::clear() {
  queue_.clear();
  phase_ = Phase::Idle;
  pendingStart_ = false;
}

PlaybackScheduler::Events PlaybackScheduler::update(uint32_t nowMs) {
  Events ev;
  if (pendingStart_) {
    ev.started = true;
    pendingStart_ = false;
  }
  for (;;) {
    switch (phase_) {
      case Phase::Idle:
        if (queue_.empty()) return ev;
        startItem(std::move(queue_.front()), nowMs);
        queue_.pop_front();
        ev.started = true;
        pendingStart_ = false;
        continue;
      case Phase::Playing:
        if (nowMs - phaseStartMs_ < lightsMs_) return ev;
        phaseStartMs_ += lightsMs_;
        phase_ = Phase::Fading;
        ev.fadeStarted = true;
        continue;
      case Phase::Fading:
        if (nowMs - phaseStartMs_ < limits::kFadeOutMs) return ev;
        phaseStartMs_ += limits::kFadeOutMs;
        phase_ = queue_.empty() ? Phase::Idle : Phase::Gap;
        ev.ended = true;
        continue;
      case Phase::Gap:
        if (nowMs - phaseStartMs_ < limits::kQueueGapMs) return ev;
        phaseStartMs_ += limits::kQueueGapMs;
        phase_ = Phase::Idle;
        continue;
    }
  }
}

}  // namespace yule
