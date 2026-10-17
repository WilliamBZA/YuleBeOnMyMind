// The eight built-in light effects. Each one renders a frame as a pure function of (pattern, seed, time), in
// normalised strip coordinates, so a pattern chosen on a 60-LED strip looks the same on an 8-LED strip.
#pragma once

#include <memory>

#include "animation.h"
#include "pattern.h"

namespace yule {

// Creates the animation for a pattern. The seed only matters for the random effects (twinkle, sparkle).
std::unique_ptr<Animation> makeEffect(const Pattern& pattern, uint32_t seed = 1);

}  // namespace yule
