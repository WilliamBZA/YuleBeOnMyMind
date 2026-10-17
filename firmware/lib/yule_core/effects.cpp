#include "effects.h"

#include <cmath>

namespace yule {

namespace {

constexpr float kPi = 3.14159265358979f;

float fract(float v) { return v - std::floor(v); }

// Normalised position of pixel i along a strip of n pixels, in (0, 1).
float pos(size_t i, size_t n) { return (static_cast<float>(i) + 0.5f) / static_cast<float>(n); }

Rgb scaled(Rgb c, float f) {
  if (f <= 0.0f) return kBlack;
  if (f >= 1.0f) return c;
  return scaleRgb(c, static_cast<uint32_t>(f * 1024.0f + 0.5f), 1024);
}

class EffectBase : public Animation {
 public:
  EffectBase(const Pattern& p, uint32_t seed) : pattern_(p), seed_(seed), factor_(speedPeriodFactor(p.speed)) {}
  void reset(uint32_t seed) override { seed_ = seed; }

 protected:
  // A base time scale in milliseconds, adjusted for the pattern's speed.
  uint32_t period(float baseMs) const {
    const float p = baseMs * factor_;
    return p < 1.0f ? 1u : static_cast<uint32_t>(p);
  }

  Pattern pattern_;
  uint32_t seed_;
  float factor_;
};

// solid: all LEDs lit steadily in colour 1; looks the same at every speed.
class Solid : public EffectBase {
 public:
  using EffectBase::EffectBase;
  void render(uint32_t, Rgb* px, size_t n) override {
    const Rgb c = pattern_.color(0);
    for (size_t i = 0; i < n; ++i) px[i] = c;
  }
};

// breathe: every LED fades up and down together; each breath uses the next colour of 1-3.
class Breathe : public EffectBase {
 public:
  using EffectBase::EffectBase;
  void render(uint32_t tMs, Rgb* px, size_t n) override {
    const uint32_t p = period(4000.0f);
    const uint32_t breath = tMs / p;
    const float phase = static_cast<float>(tMs % p) / static_cast<float>(p);
    const float level = 0.5f - 0.5f * std::cos(2.0f * kPi * phase);
    const Rgb c = scaled(pattern_.color(breath % 3), level * level);
    for (size_t i = 0; i < n; ++i) px[i] = c;
  }
};

// twinkle: random LEDs fade in and out, each in a random colour of 1-3. Stateless: every LED works out
// its own current slot from a hash.
class Twinkle : public EffectBase {
 public:
  using EffectBase::EffectBase;
  void render(uint32_t tMs, Rgb* px, size_t n) override {
    for (size_t i = 0; i < n; ++i) {
      const uint32_t h0 = hash3(seed_, static_cast<uint32_t>(i), 0xA11CEu);
      const uint32_t cycle = period(static_cast<float>(1100 + h0 % 1300));
      const uint32_t t = tMs + (h0 >> 12) % cycle;
      const uint32_t slot = t / cycle;
      const uint32_t h = hash3(seed_, static_cast<uint32_t>(i), slot + 1u);
      if ((h & 0xFFu) >= 140u) {  // about 55% of slots twinkle
        px[i] = kBlack;
        continue;
      }
      const float phase = static_cast<float>(t % cycle) / static_cast<float>(cycle);
      const float env = std::sin(kPi * phase);
      px[i] = scaled(pattern_.color((h >> 8) % 3), env * env);
    }
  }
};

// chase: three soft lights travel along the strip, in colours 1-3 in turn.
class Chase : public EffectBase {
 public:
  using EffectBase::EffectBase;
  void render(uint32_t tMs, Rgb* px, size_t n) override {
    constexpr int kDots = 3;
    const uint32_t p = period(2400.0f);
    const float head = static_cast<float>(tMs % p) / static_cast<float>(p);
    const float width = std::fmax(0.07f, 1.5f / static_cast<float>(n));
    for (size_t i = 0; i < n; ++i) {
      const float x = pos(i, n);
      float best = 0.0f;
      int bestDot = 0;
      for (int d = 0; d < kDots; ++d) {
        const float centre = fract(head + static_cast<float>(d) / kDots);
        float dist = std::fabs(x - centre);
        if (dist > 0.5f) dist = 1.0f - dist;
        const float v = 1.0f - dist / width;
        if (v > best) {
          best = v;
          bestDot = d;
        }
      }
      px[i] = best > 0.0f ? scaled(pattern_.color(static_cast<size_t>(bestDot)), best) : kBlack;
    }
  }
};

// rainbow: a full rainbow along the strip, moving. Ignores the pattern's colours.
class Rainbow : public EffectBase {
 public:
  using EffectBase::EffectBase;
  void render(uint32_t tMs, Rgb* px, size_t n) override {
    const uint32_t p = period(6000.0f);
    const float shift = static_cast<float>(tMs % p) / static_cast<float>(p);
    for (size_t i = 0; i < n; ++i) px[i] = hsvToRgb(fract(pos(i, n) + shift), 1.0f, 1.0f);
  }
};

// candy-cane: eight stripes alternating colours 1 and 2, scrolling along the strip.
class CandyCane : public EffectBase {
 public:
  using EffectBase::EffectBase;
  void render(uint32_t tMs, Rgb* px, size_t n) override {
    constexpr float kStripes = 8.0f;
    const uint32_t p = period(3000.0f);
    const float shift = kStripes * static_cast<float>(tMs % p) / static_cast<float>(p);
    for (size_t i = 0; i < n; ++i) {
      const int stripe = static_cast<int>(std::floor(pos(i, n) * kStripes - shift));
      px[i] = pattern_.color(static_cast<size_t>(stripe & 1));
    }
  }
};

// comet: a bright head with a fading tail sweeps along the strip in colour 1.
class Comet : public EffectBase {
 public:
  using EffectBase::EffectBase;
  void render(uint32_t tMs, Rgb* px, size_t n) override {
    const float tail = std::fmax(0.35f, 3.0f / static_cast<float>(n));
    const uint32_t p = period(2200.0f);
    const float head = (1.0f + tail) * static_cast<float>(tMs % p) / static_cast<float>(p);
    const Rgb c = pattern_.color(0);
    for (size_t i = 0; i < n; ++i) {
      const float d = head - pos(i, n);
      if (d < 0.0f || d > tail) {
        px[i] = kBlack;
        continue;
      }
      const float v = 1.0f - d / tail;
      px[i] = scaled(c, v * v);
    }
  }
};

// sparkle: brief flashes of colour 2 over a dim base of colour 1.
class Sparkle : public EffectBase {
 public:
  using EffectBase::EffectBase;
  void render(uint32_t tMs, Rgb* px, size_t n) override {
    const Rgb base = scaled(pattern_.color(0), 0.2f);
    const Rgb flash = pattern_.color(1);
    const uint32_t flashMs = period(160.0f);
    for (size_t i = 0; i < n; ++i) {
      const uint32_t h0 = hash3(seed_, static_cast<uint32_t>(i), 0x5BA4Cu);
      const uint32_t cycle = period(static_cast<float>(500 + h0 % 700));
      const uint32_t t = tMs + (h0 >> 12) % cycle;
      const uint32_t slot = t / cycle;
      const uint32_t inSlot = t % cycle;
      const uint32_t h = hash3(seed_, static_cast<uint32_t>(i), slot + 1u);
      if ((h & 0xFFu) < 90u && inSlot < flashMs) {  // about 35% of slots flash, briefly
        const float env = 1.0f - static_cast<float>(inSlot) / static_cast<float>(flashMs);
        px[i] = blendRgb(base, flash, env);
      } else {
        px[i] = base;
      }
    }
  }
};

}  // namespace

std::unique_ptr<Animation> makeEffect(const Pattern& pattern, uint32_t seed) {
  switch (pattern.effect) {
    case Effect::Solid: return std::make_unique<Solid>(pattern, seed);
    case Effect::Twinkle: return std::make_unique<Twinkle>(pattern, seed);
    case Effect::Chase: return std::make_unique<Chase>(pattern, seed);
    case Effect::Rainbow: return std::make_unique<Rainbow>(pattern, seed);
    case Effect::CandyCane: return std::make_unique<CandyCane>(pattern, seed);
    case Effect::Comet: return std::make_unique<Comet>(pattern, seed);
    case Effect::Sparkle: return std::make_unique<Sparkle>(pattern, seed);
    case Effect::Breathe:
    default: return std::make_unique<Breathe>(pattern, seed);
  }
}

}  // namespace yule
