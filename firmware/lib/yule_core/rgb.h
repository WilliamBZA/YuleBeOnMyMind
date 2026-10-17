// Colours and a small seedable PRNG. Hardware independent.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace yule {

struct Rgb {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;

  constexpr Rgb() = default;
  constexpr Rgb(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}

  constexpr bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
  constexpr bool operator!=(const Rgb& o) const { return !(*this == o); }
  constexpr bool isBlack() const { return r == 0 && g == 0 && b == 0; }
};

constexpr Rgb kBlack{0, 0, 0};
// The colour used when a pattern's colours are missing or invalid (a candle-like warm white).
constexpr Rgb kWarmWhite{255, 147, 41};

// Parses "#RRGGBB" (hex digits in either case). Returns false, leaving `out` untouched, for anything else.
bool parseHexColor(std::string_view text, Rgb& out);

// Formats as upper-case "#RRGGBB".
std::string formatHexColor(Rgb c);

// Scales every channel by num/den (rounding to nearest). Hue is preserved; num > den is clamped to full scale.
Rgb scaleRgb(Rgb c, uint32_t num, uint32_t den);

// Linear blend: t = 0 gives a, t = 1 gives b.
Rgb blendRgb(Rgb a, Rgb b, float t);

// h, s, v in [0, 1]. h wraps.
Rgb hsvToRgb(float h, float s, float v);

// 32-bit integer hash, used for stateless per-LED randomness in effects.
uint32_t mix32(uint32_t x);
uint32_t hash3(uint32_t a, uint32_t b, uint32_t c);

// xorshift32. Small, deterministic for a given seed, good enough for animation and jitter.
class Prng {
 public:
  explicit Prng(uint32_t seed = 1) { reseed(seed); }

  void reseed(uint32_t seed) { state_ = seed ? seed : 0x9E3779B9u; }
  uint32_t next();
  // Uniform in [0, bound). bound == 0 returns 0.
  uint32_t below(uint32_t bound);
  // Uniform in [0, 1).
  float unit();

 private:
  uint32_t state_;
};

}  // namespace yule
