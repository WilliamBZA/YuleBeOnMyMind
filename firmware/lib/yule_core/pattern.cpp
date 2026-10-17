#include "pattern.h"

namespace yule {

namespace {

const EffectInfo kEffects[kEffectCount] = {
    {Effect::Solid, "solid", "Solid", 1, "Colour 1: the steady colour"},
    {Effect::Breathe, "breathe", "Breathe", 3, "Colours 1-3: one colour per breath, in turn"},
    {Effect::Twinkle, "twinkle", "Twinkle", 3, "Colours 1-3: each twinkle picks one at random"},
    {Effect::Chase, "chase", "Chase", 3, "Colours 1-3: the travelling lights, in turn"},
    {Effect::Rainbow, "rainbow", "Rainbow", 0, "Uses no colours"},
    {Effect::CandyCane, "candy-cane", "Candy cane", 2, "Colours 1 and 2: the alternating stripes"},
    {Effect::Comet, "comet", "Comet", 1, "Colour 1: the comet and its tail"},
    {Effect::Sparkle, "sparkle", "Sparkle", 2, "Colour 1: the dim base. Colour 2: the flashes"},
};

}  // namespace

const EffectInfo& effectInfo(Effect e) {
  const size_t i = static_cast<size_t>(e);
  return kEffects[i < kEffectCount ? i : 0];
}

const EffectInfo& effectInfoAt(size_t index) { return kEffects[index < kEffectCount ? index : 0]; }

const char* effectName(Effect e) { return effectInfo(e).name; }

bool effectFromName(std::string_view name, Effect& out) {
  for (const EffectInfo& info : kEffects) {
    if (name == info.name) {
      out = info.id;
      return true;
    }
  }
  return false;
}

float speedPeriodFactor(uint8_t speed) {
  switch (speed) {
    case 1: return 2.0f;
    case 3: return 0.5f;
    default: return 1.0f;
  }
}

Rgb Pattern::color(size_t k) const {
  if (colorCount == 0) return kWarmWhite;
  const size_t n = colorCount > limits::kMaxPatternColors ? limits::kMaxPatternColors : colorCount;
  return colors[k % n];
}

bool Pattern::operator==(const Pattern& o) const {
  if (effect != o.effect || colorCount != o.colorCount || speed != o.speed) return false;
  const size_t n = colorCount > limits::kMaxPatternColors ? limits::kMaxPatternColors : colorCount;
  for (size_t i = 0; i < n; ++i) {
    if (colors[i] != o.colors[i]) return false;
  }
  return true;
}

Pattern defaultPattern() { return Pattern(); }

}  // namespace yule
