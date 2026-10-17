// A light pattern: an effect, one to three colours and a speed. This is what travels inside a press.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "rgb.h"
#include "yule_limits.h"

namespace yule {

enum class Effect : uint8_t { Solid, Breathe, Twinkle, Chase, Rainbow, CandyCane, Comet, Sparkle };

constexpr size_t kEffectCount = 8;

struct EffectInfo {
  Effect id;
  const char* name;         // wire name, e.g. "candy-cane"
  const char* label;        // shown on the settings page
  uint8_t colorsUsed;       // how many of the pattern's colours the effect uses (0 for rainbow)
  const char* colorRoles;   // what each colour does, shown next to the colour pickers
};

const EffectInfo& effectInfo(Effect e);
const EffectInfo& effectInfoAt(size_t index);  // index in [0, kEffectCount)
const char* effectName(Effect e);
bool effectFromName(std::string_view name, Effect& out);

// Speed 1, 2, 3 = slow, medium, fast. Returns the factor applied to an effect's time scales:
// slow takes twice as long as medium, fast half as long.
float speedPeriodFactor(uint8_t speed);

struct Pattern {
  Effect effect = Effect::Breathe;
  Rgb colors[limits::kMaxPatternColors] = {kWarmWhite, kBlack, kBlack};
  uint8_t colorCount = 1;  // 1..3
  uint8_t speed = limits::kDefaultSpeed;

  // Colour k (0-based). When an effect uses more colours than were supplied, the supplied ones repeat.
  Rgb color(size_t k) const;

  bool operator==(const Pattern& o) const;
  bool operator!=(const Pattern& o) const { return !(*this == o); }
};

// "breathe" in warm white at medium speed.
Pattern defaultPattern();

}  // namespace yule
