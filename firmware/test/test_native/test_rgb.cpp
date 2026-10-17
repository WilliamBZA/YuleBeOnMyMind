#include <unity.h>

#include <set>

#include "rgb.h"

using namespace yule;

static void test_parse_valid_colors() {
  Rgb c;
  TEST_ASSERT_TRUE(parseHexColor("#FF8800", c));
  TEST_ASSERT_TRUE(c == Rgb(255, 136, 0));
  TEST_ASSERT_TRUE(parseHexColor("#ff8800", c));
  TEST_ASSERT_TRUE(c == Rgb(255, 136, 0));
  TEST_ASSERT_TRUE(parseHexColor("#00aAfF", c));
  TEST_ASSERT_TRUE(c == Rgb(0, 170, 255));
  TEST_ASSERT_TRUE(parseHexColor("#000000", c));
  TEST_ASSERT_TRUE(c.isBlack());
}

static void test_parse_rejects_invalid_colors() {
  const char* bad[] = {"",        "#",       "FF8800",  "#FF880",   "#FF88001", "#GG8800",
                       "#12 456", "# 12345", "#12345g", "0xFF8800", "#ff-800",  "red"};
  for (const char* s : bad) {
    Rgb c(1, 2, 3);
    TEST_ASSERT_FALSE_MESSAGE(parseHexColor(s, c), s);
    TEST_ASSERT_TRUE_MESSAGE(c == Rgb(1, 2, 3), "output must be untouched on failure");
  }
}

static void test_format_and_round_trip() {
  TEST_ASSERT_EQUAL_STRING("#FF8800", formatHexColor(Rgb(255, 136, 0)).c_str());
  TEST_ASSERT_EQUAL_STRING("#000000", formatHexColor(Rgb(0, 0, 0)).c_str());
  TEST_ASSERT_EQUAL_STRING("#010203", formatHexColor(Rgb(1, 2, 3)).c_str());
  for (int i = 0; i < 256; i += 15) {
    Rgb in(static_cast<uint8_t>(i), static_cast<uint8_t>(255 - i), static_cast<uint8_t>(i / 2)), out;
    TEST_ASSERT_TRUE(parseHexColor(formatHexColor(in), out));
    TEST_ASSERT_TRUE(in == out);
  }
}

static void test_warm_white_default() {
  TEST_ASSERT_FALSE(kWarmWhite.isBlack());
  // Warm: more red than green, more green than blue.
  TEST_ASSERT_TRUE(kWarmWhite.r > kWarmWhite.g);
  TEST_ASSERT_TRUE(kWarmWhite.g > kWarmWhite.b);
}

static void test_prng_is_deterministic_for_a_seed() {
  Prng a(12345), b(12345), c(54321);
  bool differs = false;
  for (int i = 0; i < 32; ++i) {
    const uint32_t va = a.next();
    TEST_ASSERT_EQUAL_UINT32(va, b.next());
    if (va != c.next()) differs = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(differs, "different seeds must give different sequences");
}

static void test_prng_reseed_restarts_sequence() {
  Prng a(7);
  const uint32_t first = a.next();
  a.next();
  a.reseed(7);
  TEST_ASSERT_EQUAL_UINT32(first, a.next());
}

static void test_prng_zero_seed_does_not_get_stuck() {
  Prng p(0);
  std::set<uint32_t> seen;
  for (int i = 0; i < 16; ++i) seen.insert(p.next());
  TEST_ASSERT_TRUE(seen.size() > 8);
}

static void test_prng_ranges() {
  Prng p(99);
  for (int i = 0; i < 2000; ++i) {
    TEST_ASSERT_TRUE(p.below(7) < 7);
    const float u = p.unit();
    TEST_ASSERT_TRUE(u >= 0.0f && u < 1.0f);
  }
  TEST_ASSERT_EQUAL_UINT32(0, p.below(0));
  TEST_ASSERT_EQUAL_UINT32(0, p.below(1));
}

static void test_scale_keeps_hue() {
  const Rgb half = scaleRgb(kWarmWhite, 50, 100);
  TEST_ASSERT_UINT8_WITHIN(1, 128, half.r);
  TEST_ASSERT_UINT8_WITHIN(1, 74, half.g);
  TEST_ASSERT_UINT8_WITHIN(1, 21, half.b);
  TEST_ASSERT_TRUE(scaleRgb(kWarmWhite, 100, 100) == kWarmWhite);
  TEST_ASSERT_TRUE(scaleRgb(kWarmWhite, 0, 100).isBlack());
}

static void test_hsv_primaries() {
  TEST_ASSERT_TRUE(hsvToRgb(0.0f, 1.0f, 1.0f) == Rgb(255, 0, 0));
  TEST_ASSERT_TRUE(hsvToRgb(1.0f / 3.0f, 1.0f, 1.0f) == Rgb(0, 255, 0));
  TEST_ASSERT_TRUE(hsvToRgb(2.0f / 3.0f, 1.0f, 1.0f) == Rgb(0, 0, 255));
  TEST_ASSERT_TRUE(hsvToRgb(0.25f, 0.0f, 1.0f) == Rgb(255, 255, 255));
  TEST_ASSERT_TRUE(hsvToRgb(0.4f, 1.0f, 0.0f).isBlack());
  // Hue wraps.
  TEST_ASSERT_TRUE(hsvToRgb(1.0f, 1.0f, 1.0f) == hsvToRgb(0.0f, 1.0f, 1.0f));
  TEST_ASSERT_TRUE(hsvToRgb(-1.0f / 3.0f, 1.0f, 1.0f) == hsvToRgb(2.0f / 3.0f, 1.0f, 1.0f));
}

static void test_blend() {
  TEST_ASSERT_TRUE(blendRgb(Rgb(0, 0, 0), Rgb(200, 100, 50), 0.0f) == Rgb(0, 0, 0));
  TEST_ASSERT_TRUE(blendRgb(Rgb(0, 0, 0), Rgb(200, 100, 50), 1.0f) == Rgb(200, 100, 50));
  const Rgb mid = blendRgb(Rgb(0, 0, 0), Rgb(200, 100, 50), 0.5f);
  TEST_ASSERT_UINT8_WITHIN(1, 100, mid.r);
  TEST_ASSERT_UINT8_WITHIN(1, 50, mid.g);
}

void run_rgb_tests() {
  RUN_TEST(test_parse_valid_colors);
  RUN_TEST(test_parse_rejects_invalid_colors);
  RUN_TEST(test_format_and_round_trip);
  RUN_TEST(test_warm_white_default);
  RUN_TEST(test_prng_is_deterministic_for_a_seed);
  RUN_TEST(test_prng_reseed_restarts_sequence);
  RUN_TEST(test_prng_zero_seed_does_not_get_stuck);
  RUN_TEST(test_prng_ranges);
  RUN_TEST(test_scale_keeps_hue);
  RUN_TEST(test_hsv_primaries);
  RUN_TEST(test_blend);
}
