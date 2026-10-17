#include <unity.h>

#include <set>
#include <string>
#include <vector>

#include "effects.h"
#include "pattern.h"
#include "rgb.h"

using namespace yule;

namespace {

constexpr Rgb kSentinel{3, 251, 7};  // a colour no effect produces from the test patterns

Pattern makePattern(Effect e, std::vector<Rgb> colors, uint8_t speed = 2) {
  Pattern p;
  p.effect = e;
  p.colorCount = static_cast<uint8_t>(colors.size());
  for (size_t i = 0; i < colors.size() && i < 3; ++i) p.colors[i] = colors[i];
  p.speed = speed;
  return p;
}

std::vector<Rgb> frame(const Pattern& p, uint32_t t, size_t n, uint32_t seed = 1) {
  auto fx = makeEffect(p, seed);
  std::vector<Rgb> px(n, kSentinel);
  fx->render(t, px.data(), n);
  return px;
}

bool sameFrame(const std::vector<Rgb>& a, const std::vector<Rgb>& b) { return a == b; }

}  // namespace

static void test_every_effect_stays_in_bounds_at_every_length() {
  const size_t lengths[] = {1, 8, 60, 300};
  const uint32_t times[] = {0, 1, 17, 250, 999, 1500, 4321, 30000, 123456};
  for (size_t e = 0; e < kEffectCount; ++e) {
    for (size_t n : lengths) {
      for (uint8_t speed = 1; speed <= 3; ++speed) {
        Pattern p = makePattern(effectInfoAt(e).id, {Rgb(200, 30, 10), Rgb(10, 200, 30), Rgb(30, 10, 200)}, speed);
        auto fx = makeEffect(p, 42);
        constexpr size_t kGuard = 4;
        for (uint32_t t : times) {
          std::vector<Rgb> buf(n + 2 * kGuard, kSentinel);
          fx->render(t, buf.data() + kGuard, n);
          for (size_t g = 0; g < kGuard; ++g) {
            TEST_ASSERT_TRUE_MESSAGE(buf[g] == kSentinel, effectInfoAt(e).name);
            TEST_ASSERT_TRUE_MESSAGE(buf[kGuard + n + g] == kSentinel, effectInfoAt(e).name);
          }
          for (size_t i = 0; i < n; ++i) {
            TEST_ASSERT_TRUE_MESSAGE(buf[kGuard + i] != kSentinel, (std::string(effectInfoAt(e).name) + " left a pixel unwritten").c_str());
          }
        }
      }
    }
  }
}

static void test_effect_names_round_trip() {
  for (size_t e = 0; e < kEffectCount; ++e) {
    Effect out = Effect::Solid;
    TEST_ASSERT_TRUE(effectFromName(effectInfoAt(e).name, out));
    TEST_ASSERT_TRUE(out == effectInfoAt(e).id);
    TEST_ASSERT_EQUAL_STRING(effectInfoAt(e).name, effectName(effectInfoAt(e).id));
  }
  Effect out = Effect::Solid;
  TEST_ASSERT_FALSE(effectFromName("plasma", out));
  TEST_ASSERT_FALSE(effectFromName("", out));
  TEST_ASSERT_FALSE(effectFromName("Solid", out));  // wire names are lower case
  TEST_ASSERT_EQUAL_UINT32(8, kEffectCount);
}

static void test_default_pattern() {
  const Pattern p = defaultPattern();
  TEST_ASSERT_TRUE(p.effect == Effect::Breathe);
  TEST_ASSERT_EQUAL_UINT8(1, p.colorCount);
  TEST_ASSERT_TRUE(p.colors[0] == kWarmWhite);
  TEST_ASSERT_EQUAL_UINT8(2, p.speed);
}

static void test_colors_repeat_when_fewer_than_used() {
  const Pattern one = makePattern(Effect::Chase, {Rgb(1, 2, 3)});
  for (size_t k = 0; k < 6; ++k) TEST_ASSERT_TRUE(one.color(k) == Rgb(1, 2, 3));
  const Pattern two = makePattern(Effect::Chase, {Rgb(10, 0, 0), Rgb(0, 10, 0)});
  TEST_ASSERT_TRUE(two.color(0) == Rgb(10, 0, 0));
  TEST_ASSERT_TRUE(two.color(1) == Rgb(0, 10, 0));
  TEST_ASSERT_TRUE(two.color(2) == Rgb(10, 0, 0));  // repeats from the start
  TEST_ASSERT_TRUE(two.color(3) == Rgb(0, 10, 0));
}

static void test_candy_cane_alternates_colors_and_scrolls() {
  const Rgb red(255, 0, 0), white(255, 255, 255);
  const Pattern p = makePattern(Effect::CandyCane, {red, white});
  const auto f0 = frame(p, 0, 60);
  bool hasRed = false, hasWhite = false;
  int transitions = 0;
  for (size_t i = 0; i < f0.size(); ++i) {
    TEST_ASSERT_TRUE(f0[i] == red || f0[i] == white);
    hasRed |= f0[i] == red;
    hasWhite |= f0[i] == white;
    if (i > 0 && f0[i] != f0[i - 1]) ++transitions;
  }
  TEST_ASSERT_TRUE(hasRed && hasWhite);
  TEST_ASSERT_TRUE(transitions >= 6 && transitions <= 8);  // eight stripes along the strip

  // It scrolls: a stripe's width of time later, red and white have swapped places.
  const auto s0 = frame(p, 0, 8);
  const auto s1 = frame(p, 375, 8);  // 3000 ms for eight stripes
  for (size_t i = 0; i < 8; ++i) TEST_ASSERT_TRUE(s0[i] != s1[i]);
  TEST_ASSERT_FALSE(sameFrame(frame(p, 0, 60), frame(p, 100, 60)));
}

static void test_candy_cane_looks_the_same_on_short_and_long_strips() {
  const Rgb red(255, 0, 0), white(255, 255, 255);
  const Pattern p = makePattern(Effect::CandyCane, {red, white});
  for (size_t n : {8u, 24u, 60u, 300u}) {
    const auto f = frame(p, 0, n);
    int transitions = 0;
    for (size_t i = 1; i < n; ++i) transitions += f[i] != f[i - 1];
    TEST_ASSERT_EQUAL_INT_MESSAGE(7, transitions, std::to_string(n).c_str());
  }
}

static void test_solid_is_identical_at_every_speed_and_time() {
  const Rgb c(12, 200, 90);
  const auto ref = frame(makePattern(Effect::Solid, {c}, 2), 0, 20);
  for (size_t i = 0; i < ref.size(); ++i) TEST_ASSERT_TRUE(ref[i] == c);
  for (uint8_t speed = 1; speed <= 3; ++speed) {
    for (uint32_t t : {0u, 333u, 5000u, 99999u}) {
      TEST_ASSERT_TRUE(sameFrame(ref, frame(makePattern(Effect::Solid, {c}, speed), t, 20)));
    }
  }
}

static void test_rainbow_ignores_colors() {
  const Pattern a = makePattern(Effect::Rainbow, {Rgb(255, 0, 0)});
  const Pattern b = makePattern(Effect::Rainbow, {Rgb(0, 0, 255), Rgb(9, 9, 9), Rgb(50, 60, 70)});
  for (uint32_t t : {0u, 700u, 4000u}) TEST_ASSERT_TRUE(sameFrame(frame(a, t, 40), frame(b, t, 40)));
  // And it moves, and shows several hues.
  TEST_ASSERT_FALSE(sameFrame(frame(a, 0, 40), frame(a, 1000, 40)));
  std::set<uint32_t> distinct;
  for (const Rgb& c : frame(a, 0, 40)) distinct.insert(c.r * 65536u + c.g * 256u + c.b);
  TEST_ASSERT_TRUE(distinct.size() > 20);
}

static void test_chase_with_one_color_uses_only_that_color() {
  const Rgb red(200, 0, 0);
  const Pattern p = makePattern(Effect::Chase, {red});
  bool anyLit = false;
  for (uint32_t t = 0; t < 5000; t += 137) {
    for (const Rgb& c : frame(p, t, 60)) {
      TEST_ASSERT_EQUAL_UINT8(0, c.g);
      TEST_ASSERT_EQUAL_UINT8(0, c.b);
      TEST_ASSERT_TRUE(c.r <= red.r);
      anyLit |= c.r > 0;
    }
  }
  TEST_ASSERT_TRUE(anyLit);
}

static void test_chase_with_three_colors_shows_all_three() {
  const Pattern p = makePattern(Effect::Chase, {Rgb(255, 0, 0), Rgb(0, 255, 0), Rgb(0, 0, 255)});
  bool r = false, g = false, b = false;
  for (const Rgb& c : frame(p, 0, 60)) {
    r |= c.r > 100;
    g |= c.g > 100;
    b |= c.b > 100;
  }
  TEST_ASSERT_TRUE(r && g && b);
}

static int upwardCrossings(const Pattern& p, uint32_t durationMs) {
  auto fx = makeEffect(p, 1);
  int crossings = 0;
  bool above = false;
  Rgb px;
  for (uint32_t t = 0; t < durationMs; t += 20) {
    fx->render(t, &px, 1);
    const bool nowAbove = px.r > 64;
    if (nowAbove && !above) ++crossings;
    above = nowAbove;
  }
  return crossings;
}

static void test_fast_breathe_completes_more_cycles_than_slow() {
  const Rgb red(255, 0, 0);
  const int slow = upwardCrossings(makePattern(Effect::Breathe, {red}, 1), 16000);
  const int medium = upwardCrossings(makePattern(Effect::Breathe, {red}, 2), 16000);
  const int fast = upwardCrossings(makePattern(Effect::Breathe, {red}, 3), 16000);
  TEST_ASSERT_EQUAL_INT(2, slow);    // 8 s per breath
  TEST_ASSERT_EQUAL_INT(4, medium);  // 4 s per breath
  TEST_ASSERT_EQUAL_INT(8, fast);    // 2 s per breath
  TEST_ASSERT_TRUE(fast > medium && medium > slow);
}

static void test_breathe_cycles_through_colors_one_per_breath() {
  const Rgb a(255, 0, 0), b(0, 255, 0), c(0, 0, 255);
  auto fx = makeEffect(makePattern(Effect::Breathe, {a, b, c}, 2), 1);
  const Rgb expect[] = {a, b, c, a, b};
  for (uint32_t k = 0; k < 5; ++k) {
    Rgb px;
    fx->render(4000 * k + 2000, &px, 1);  // the peak of breath k
    TEST_ASSERT_TRUE(px == expect[k]);
  }
  // With two colours the supplied colours repeat: A, B, A.
  auto two = makeEffect(makePattern(Effect::Breathe, {a, b}, 2), 1);
  Rgb px;
  two->render(2000, &px, 1);
  TEST_ASSERT_TRUE(px == a);
  two->render(6000, &px, 1);
  TEST_ASSERT_TRUE(px == b);
  two->render(10000, &px, 1);
  TEST_ASSERT_TRUE(px == a);
}

static void test_each_speed_step_is_twice_as_fast() {
  // Frames repeat after one period; slow is twice, fast half, of medium.
  // (Breathe uses one colour here because it changes colour on every breath.)
  struct Case { Effect e; uint32_t mediumPeriod; size_t colors; };
  const Case cases[] = {{Effect::Breathe, 4000, 1}, {Effect::Chase, 2400, 2}, {Effect::Rainbow, 6000, 2},
                        {Effect::CandyCane, 3000, 2}, {Effect::Comet, 2200, 2}};
  for (const Case& c : cases) {
    for (uint8_t speed = 1; speed <= 3; ++speed) {
      const float factor = speed == 1 ? 2.0f : (speed == 2 ? 1.0f : 0.5f);
      const uint32_t period = static_cast<uint32_t>(c.mediumPeriod * factor);
      std::vector<Rgb> colors = {Rgb(255, 0, 0), Rgb(0, 0, 255)};
      colors.resize(c.colors);
      const Pattern p = makePattern(c.e, colors, speed);
      TEST_ASSERT_TRUE_MESSAGE(sameFrame(frame(p, 700, 30), frame(p, 700 + period, 30)), effectName(c.e));
      TEST_ASSERT_FALSE_MESSAGE(sameFrame(frame(p, 700, 30), frame(p, 700 + period / 3, 30)), effectName(c.e));
    }
  }
}

static void test_comet_has_a_bright_head_and_fading_tail() {
  const Pattern p = makePattern(Effect::Comet, {Rgb(255, 255, 255)});
  const auto f = frame(p, 600, 60);  // part-way along the sweep
  int head = -1;
  for (size_t i = 0; i < f.size(); ++i) {
    if (f[i].r > 0 && (head < 0 || f[i].r >= f[head].r)) head = static_cast<int>(i);
  }
  TEST_ASSERT_TRUE(head > 0);
  // Brightness falls off behind the head and nothing is lit ahead of it.
  for (int i = head - 1; i > 0 && f[i].r > 0; --i) TEST_ASSERT_TRUE(f[i].r <= f[i + 1].r);
  for (size_t i = head + 1; i < f.size(); ++i) TEST_ASSERT_EQUAL_UINT8(0, f[i].r);
}

static void test_twinkle_is_deterministic_per_seed_and_varied() {
  const Pattern p = makePattern(Effect::Twinkle, {Rgb(200, 16, 46), Rgb(0, 132, 61)});
  TEST_ASSERT_TRUE(sameFrame(frame(p, 1234, 60, 7), frame(p, 1234, 60, 7)));
  TEST_ASSERT_FALSE(sameFrame(frame(p, 1234, 60, 7), frame(p, 1234, 60, 8)));
  int lit = 0, dark = 0;
  for (uint32_t t = 0; t < 6000; t += 250) {
    for (const Rgb& c : frame(p, t, 60, 7)) (c.isBlack() ? dark : lit)++;
  }
  TEST_ASSERT_TRUE(lit > 100 && dark > 100);
  // Colours come only from the pattern's colours (scaled): hue never mixes the two.
  for (const Rgb& c : frame(p, 800, 60, 7)) TEST_ASSERT_TRUE(c.r == 0 || c.g == 0 || (c.r > c.g) != (c.g > c.r) || c.isBlack());
}

static void test_sparkle_has_dim_base_and_flashes() {
  const Rgb base(0, 100, 0), flash(255, 255, 255);
  const Pattern p = makePattern(Effect::Sparkle, {base, flash});
  const Rgb dimBase = scaleRgb(base, 1024 / 5, 1024);  // about 20%
  bool sawFlash = false;
  for (uint32_t t = 0; t < 4000; t += 20) {
    for (const Rgb& c : frame(p, t, 40, 5)) {
      TEST_ASSERT_TRUE(c.g >= dimBase.g - 1);  // never darker than the base
      if (c.r > 100) sawFlash = true;
    }
  }
  TEST_ASSERT_TRUE(sawFlash);
  // With one colour, the flash repeats colour 1 instead of being white.
  const Pattern solo = makePattern(Effect::Sparkle, {base});
  for (uint32_t t = 0; t < 2000; t += 20) {
    for (const Rgb& c : frame(solo, t, 40, 5)) {
      TEST_ASSERT_EQUAL_UINT8(0, c.r);
      TEST_ASSERT_EQUAL_UINT8(0, c.b);
    }
  }
}

void run_effects_tests() {
  RUN_TEST(test_every_effect_stays_in_bounds_at_every_length);
  RUN_TEST(test_effect_names_round_trip);
  RUN_TEST(test_default_pattern);
  RUN_TEST(test_colors_repeat_when_fewer_than_used);
  RUN_TEST(test_candy_cane_alternates_colors_and_scrolls);
  RUN_TEST(test_candy_cane_looks_the_same_on_short_and_long_strips);
  RUN_TEST(test_solid_is_identical_at_every_speed_and_time);
  RUN_TEST(test_rainbow_ignores_colors);
  RUN_TEST(test_chase_with_one_color_uses_only_that_color);
  RUN_TEST(test_chase_with_three_colors_shows_all_three);
  RUN_TEST(test_fast_breathe_completes_more_cycles_than_slow);
  RUN_TEST(test_breathe_cycles_through_colors_one_per_breath);
  RUN_TEST(test_each_speed_step_is_twice_as_fast);
  RUN_TEST(test_comet_has_a_bright_head_and_fading_tail);
  RUN_TEST(test_twinkle_is_deterministic_per_seed_and_varied);
  RUN_TEST(test_sparkle_has_dim_base_and_flashes);
}
