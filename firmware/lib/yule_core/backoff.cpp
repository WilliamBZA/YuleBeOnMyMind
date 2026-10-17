#include "backoff.h"

namespace yule {

uint32_t Backoff::ceilingMs(uint32_t attempt) const {
  uint64_t c = baseMs_;
  for (uint32_t i = 0; i < attempt && c < capMs_; ++i) c *= 2;
  return c > capMs_ ? capMs_ : static_cast<uint32_t>(c);
}

uint32_t Backoff::nextDelayMs() {
  const uint32_t ceiling = ceilingMs(attempt_);
  if (attempt_ < 64) ++attempt_;
  const uint32_t jitterRange = ceiling / 4;
  const uint32_t delay = ceiling - prng_.below(jitterRange + 1);
  return delay > capMs_ ? capMs_ : delay;
}

}  // namespace yule
