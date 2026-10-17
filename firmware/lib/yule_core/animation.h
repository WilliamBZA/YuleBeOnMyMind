// The one interface everything that draws on the strip implements: effects, feedback cues and status
// indications. Hardware independent.
#pragma once

#include <cstddef>
#include <cstdint>

#include "rgb.h"

namespace yule {

class Animation {
 public:
  virtual ~Animation() = default;

  // Restarts any random state. Effects are otherwise a pure function of (seed, time, pixel).
  virtual void reset(uint32_t seed) { (void)seed; }

  // Writes the full frame for `tMs` milliseconds after the animation started. Every one of the n pixels
  // is written; pixels outside [0, n) are never touched. Output is at full brightness: the compositor
  // applies the owner's brightness afterwards.
  virtual void render(uint32_t tMs, Rgb* px, size_t n) = 0;

  // How long the animation lasts, or 0 if it runs until it is replaced or cleared.
  virtual uint32_t durationMs() const { return 0; }
};

}  // namespace yule
