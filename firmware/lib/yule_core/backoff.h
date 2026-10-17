// Exponential back-off with jitter for Wi-Fi and broker reconnection, capped at 60 s. Hardware independent.
#pragma once

#include <cstdint>

#include "rgb.h"  // Prng
#include "yule_limits.h"

namespace yule {

class Backoff {
 public:
  explicit Backoff(uint32_t baseMs = 1000, uint32_t capMs = limits::kBackoffCapMs, uint32_t seed = 1)
      : baseMs_(baseMs ? baseMs : 1), capMs_(capMs), prng_(seed) {}

  // The delay to wait before the next attempt, then advances. Each call is about twice the previous one
  // (with up to 25% jitter below the ceiling) until the cap.
  uint32_t nextDelayMs();

  // Call after a successful connection.
  void reset() { attempt_ = 0; }
  uint32_t attempts() const { return attempt_; }

  // The un-jittered upper bound for attempt number `attempt` (0-based): base * 2^attempt, capped.
  uint32_t ceilingMs(uint32_t attempt) const;

 private:
  uint32_t baseMs_;
  uint32_t capMs_;
  uint32_t attempt_ = 0;
  Prng prng_;
};

}  // namespace yule
