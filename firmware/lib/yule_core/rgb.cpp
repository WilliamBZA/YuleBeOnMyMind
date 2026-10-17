#include "rgb.h"

#include <cmath>

namespace yule {

namespace {

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

uint8_t clamp8(float v) {
  if (v <= 0.0f) return 0;
  if (v >= 255.0f) return 255;
  return static_cast<uint8_t>(v + 0.5f);
}

}  // namespace

bool parseHexColor(std::string_view text, Rgb& out) {
  if (text.size() != 7 || text[0] != '#') return false;
  int v[6];
  for (size_t i = 0; i < 6; ++i) {
    v[i] = hexValue(text[i + 1]);
    if (v[i] < 0) return false;
  }
  out = Rgb(static_cast<uint8_t>(v[0] * 16 + v[1]), static_cast<uint8_t>(v[2] * 16 + v[3]),
            static_cast<uint8_t>(v[4] * 16 + v[5]));
  return true;
}

std::string formatHexColor(Rgb c) {
  static const char kDigits[] = "0123456789ABCDEF";
  std::string s = "#000000";
  s[1] = kDigits[c.r >> 4];
  s[2] = kDigits[c.r & 0xF];
  s[3] = kDigits[c.g >> 4];
  s[4] = kDigits[c.g & 0xF];
  s[5] = kDigits[c.b >> 4];
  s[6] = kDigits[c.b & 0xF];
  return s;
}

Rgb scaleRgb(Rgb c, uint32_t num, uint32_t den) {
  if (den == 0 || num >= den) return c;
  auto s = [&](uint8_t v) { return static_cast<uint8_t>((static_cast<uint32_t>(v) * num + den / 2) / den); };
  return Rgb(s(c.r), s(c.g), s(c.b));
}

Rgb blendRgb(Rgb a, Rgb b, float t) {
  if (t <= 0.0f) return a;
  if (t >= 1.0f) return b;
  auto m = [&](uint8_t x, uint8_t y) { return clamp8(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t); };
  return Rgb(m(a.r, b.r), m(a.g, b.g), m(a.b, b.b));
}

Rgb hsvToRgb(float h, float s, float v) {
  h -= std::floor(h);
  s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
  v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
  const float h6 = h * 6.0f;
  const int sector = static_cast<int>(h6) % 6;
  const float f = h6 - std::floor(h6);
  const float p = v * (1.0f - s);
  const float q = v * (1.0f - s * f);
  const float t = v * (1.0f - s * (1.0f - f));
  float r = 0, g = 0, b = 0;
  switch (sector) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return Rgb(clamp8(r * 255.0f), clamp8(g * 255.0f), clamp8(b * 255.0f));
}

uint32_t mix32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
  return mix32(a * 0x9E3779B1u ^ mix32(b + 0x85EBCA6Bu) ^ mix32(c * 0xC2B2AE35u + 0x27D4EB2Fu));
}

uint32_t Prng::next() {
  uint32_t x = state_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  state_ = x;
  return x;
}

uint32_t Prng::below(uint32_t bound) {
  if (bound == 0) return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(next()) * bound) >> 32);
}

float Prng::unit() { return static_cast<float>(next() >> 8) / 16777216.0f; }

}  // namespace yule
