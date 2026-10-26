#include <unity.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "compositor.h"
#include "cues.h"
#include "effects.h"
#include "status_indication.h"

using namespace yule;

namespace {

// Base-layer test animation whose red channel counts up with its own elapsed time.
class Ramp : public Animation {
 public:
  void render(uint32_t tMs, Rgb* px, size_t n) override {
    const uint8_t v = static_cast<uint8_t>(std::min<uint32_t>(255, tMs / 10));
    for (size_t i = 0; i < n; ++i) px[i] = Rgb(v, 0, 0);
  }
};

class Flat : public Animation {
 public:
  Flat(Rgb c, uint32_t durationMs = 0) : c_(c), dur_(durationMs) {}
  uint32_t durationMs() const override { return dur_; }
  void render(uint32_t, Rgb* px, size_t n) override {
    for (size_t i = 0; i < n; ++i) px[i] = c_;
  }

 private:
  Rgb c_;
  uint32_t dur_;
};

std::vector<Rgb> draw(Compositor& c, uint32_t t, size_t n = 8) {
  std::vector<Rgb> out(n, Rgb(9, 9, 9));
  c.render(t, out.data(), n);
  return out;
}

int litCount(const std::vector<Rgb>& f) {
  int n = 0;
  for (const Rgb& c : f) n += !c.isBlack();
  return n;
}

Rgb maxOf(Animation& a, uint32_t durationMs, size_t n = 8) {
  Rgb m;
  std::vector<Rgb> px(n);
  for (uint32_t t = 0; t < durationMs; t += 5) {
    a.render(t, px.data(), n);
    for (const Rgb& c : px) {
      m.r = std::max(m.r, c.r);
      m.g = std::max(m.g, c.g);
      m.b = std::max(m.b, c.b);
    }
  }
  return m;
}

}  // namespace

// ---- compositor ----

static void test_idle_compositor_is_dark() {
  Compositor c;
  TEST_ASSERT_FALSE(c.hasBase());
  TEST_ASSERT_EQUAL_INT(0, litCount(draw(c, 1000)));
}

static void test_overlay_replacement_and_resumption() {
  Compositor c;
  c.setBrightness(100);
  c.setBase(std::make_shared<Ramp>(), 0);
  TEST_ASSERT_EQUAL_UINT8(100, draw(c, 1000)[0].r);

  c.setOverlay(std::make_shared<Flat>(Rgb(0, 255, 0), 500), 1000);  // would end at 1500
  TEST_ASSERT_TRUE(draw(c, 1200)[3] == Rgb(0, 255, 0));
  c.setOverlay(std::make_shared<Flat>(Rgb(0, 0, 255), 500), 1300);  // replaces it, ends at 1800
  TEST_ASSERT_TRUE(draw(c, 1400)[0] == Rgb(0, 0, 255));
  TEST_ASSERT_TRUE_MESSAGE(draw(c, 1600)[0] == Rgb(0, 0, 255), "the first overlay's expiry must not cut the second short");

  // The overlay expires and the base resumes where its own timeline has got to, not from the start.
  TEST_ASSERT_EQUAL_UINT8(180, draw(c, 1800)[0].r);
  TEST_ASSERT_FALSE(c.hasOverlay());
  TEST_ASSERT_EQUAL_UINT8(200, draw(c, 2000)[0].r);
}

static void test_overlay_with_no_duration_lasts_until_cleared() {
  Compositor c;
  c.setBrightness(100);
  c.setOverlay(std::make_shared<Flat>(Rgb(0, 0, 255)), 100);
  TEST_ASSERT_TRUE(draw(c, 100000)[0] == Rgb(0, 0, 255));
  TEST_ASSERT_TRUE(c.hasOverlay());
  c.clearOverlay();
  TEST_ASSERT_FALSE(c.hasOverlay());
  TEST_ASSERT_EQUAL_INT(0, litCount(draw(c, 100001)));
}

static void test_overlay_covers_the_whole_base() {
  Compositor c;
  c.setBrightness(100);
  c.setBase(std::make_shared<Flat>(Rgb(255, 255, 255)), 0);
  // A cue that lights only some pixels must not let the base show through the rest.
  class FirstOnly : public Animation {
   public:
    uint32_t durationMs() const override { return 1000; }
    void render(uint32_t, Rgb* px, size_t n) override {
      for (size_t i = 0; i < n; ++i) px[i] = i == 0 ? Rgb(255, 0, 0) : kBlack;
    }
  };
  c.setOverlay(std::make_shared<FirstOnly>(), 0);
  TEST_ASSERT_EQUAL_INT(1, litCount(draw(c, 10)));
}

static void test_fade_out_reaches_black_after_about_one_second() {
  Compositor c;
  c.setBrightness(100);
  c.setBase(std::make_shared<Flat>(Rgb(255, 255, 255)), 0);
  TEST_ASSERT_EQUAL_UINT8(255, draw(c, 5000)[0].r);
  c.startFadeOut(5000);
  TEST_ASSERT_TRUE(c.fading());
  TEST_ASSERT_EQUAL_UINT8(255, draw(c, 5000)[0].r);
  TEST_ASSERT_UINT8_WITHIN(2, 128, draw(c, 5500)[0].r);
  uint8_t previous = 255;
  for (uint32_t t = 5000; t < 6000; t += 50) {
    const uint8_t v = draw(c, t)[0].r;
    TEST_ASSERT_TRUE(v <= previous);  // smooth, monotonic
    previous = v;
  }
  TEST_ASSERT_TRUE(draw(c, 5990)[0].r <= 3);
  TEST_ASSERT_TRUE(c.hasBase());
  TEST_ASSERT_EQUAL_INT(0, litCount(draw(c, 6000)));
  TEST_ASSERT_FALSE_MESSAGE(c.hasBase(), "the base is cleared once the fade has finished");
  TEST_ASSERT_FALSE(c.fading());
  TEST_ASSERT_EQUAL_INT(0, litCount(draw(c, 7000)));
}

static void test_setting_a_new_base_cancels_a_fade() {
  Compositor c;
  c.setBrightness(100);
  c.setBase(std::make_shared<Flat>(Rgb(255, 255, 255)), 0);
  c.startFadeOut(100);
  c.setBase(std::make_shared<Flat>(Rgb(0, 255, 0)), 400);
  TEST_ASSERT_FALSE(c.fading());
  TEST_ASSERT_TRUE(draw(c, 900)[0] == Rgb(0, 255, 0));
}

static void test_fade_with_no_base_does_nothing() {
  Compositor c;
  c.startFadeOut(0);
  TEST_ASSERT_FALSE(c.fading());
}

static void test_brightness_scales_without_changing_hue() {
  Compositor c;
  c.setBase(std::make_shared<Flat>(Rgb(200, 100, 40)), 0);
  c.setBrightness(100);
  TEST_ASSERT_TRUE(draw(c, 0)[0] == Rgb(200, 100, 40));
  c.setBrightness(50);
  TEST_ASSERT_TRUE(draw(c, 0)[0] == Rgb(100, 50, 20));
  c.setBrightness(20);
  TEST_ASSERT_TRUE(draw(c, 0)[0] == Rgb(40, 20, 8));
  // Ratios between channels are kept (within rounding) at every setting.
  for (uint8_t b = 5; b <= 100; b += 5) {
    c.setBrightness(b);
    const Rgb px = draw(c, 0)[0];
    TEST_ASSERT_UINT8_WITHIN(1, static_cast<uint8_t>(200 * b / 100), px.r);
    TEST_ASSERT_UINT8_WITHIN(1, static_cast<uint8_t>(100 * b / 100), px.g);
    TEST_ASSERT_UINT8_WITHIN(1, static_cast<uint8_t>(40 * b / 100), px.b);
  }
}

static void test_brightness_is_clamped_to_5_to_100() {
  Compositor c;
  c.setBrightness(0);
  TEST_ASSERT_EQUAL_UINT8(5, c.brightness());
  c.setBrightness(4);
  TEST_ASSERT_EQUAL_UINT8(5, c.brightness());
  c.setBrightness(250);
  TEST_ASSERT_EQUAL_UINT8(100, c.brightness());
  c.setBrightness(37);
  TEST_ASSERT_EQUAL_UINT8(37, c.brightness());
}

static void test_brightness_applies_to_overlays_too() {
  Compositor c;
  c.setBrightness(50);
  c.setOverlay(std::make_shared<Flat>(Rgb(0, 200, 0)), 0);
  TEST_ASSERT_EQUAL_UINT8(100, draw(c, 10)[0].g);
}

static void test_render_writes_exactly_n_pixels() {
  Compositor c;
  c.setBase(std::make_shared<Flat>(Rgb(1, 2, 3)), 0);
  for (size_t n : {1u, 8u, 60u, 300u}) {
    std::vector<Rgb> buf(n + 2, Rgb(77, 77, 77));
    c.render(5, buf.data() + 1, n);
    TEST_ASSERT_TRUE(buf.front() == Rgb(77, 77, 77));
    TEST_ASSERT_TRUE(buf.back() == Rgb(77, 77, 77));
  }
}

static void test_base_with_effect_keeps_its_own_timeline() {
  Compositor c;
  c.setBrightness(100);
  Pattern p;
  p.effect = Effect::Rainbow;
  c.setBase(std::shared_ptr<Animation>(makeEffect(p, 1)), 1000);
  const auto before = draw(c, 3000, 20);
  c.setOverlay(std::make_shared<Flat>(Rgb(0, 255, 0), 700), 3100);
  draw(c, 3500, 20);
  auto reference = std::shared_ptr<Animation>(makeEffect(p, 1));
  std::vector<Rgb> expect(20);
  reference->render(4000 - 1000, expect.data(), 20);
  TEST_ASSERT_TRUE(draw(c, 4000, 20) == expect);
  TEST_ASSERT_FALSE(draw(c, 4000, 20) == before);
}

// ---- cues and status indications ----

static void test_cue_durations() {
  TEST_ASSERT_EQUAL_UINT32(900, cueDurationMs(Cue::Sent));
  TEST_ASSERT_EQUAL_UINT32(900, cueDurationMs(Cue::SendFailed));
  TEST_ASSERT_EQUAL_UINT32(600, cueDurationMs(Cue::PleaseWait));
  TEST_ASSERT_EQUAL_UINT32(800, cueDurationMs(Cue::Muted));
  TEST_ASSERT_EQUAL_UINT32(800, cueDurationMs(Cue::Unmuted));
  TEST_ASSERT_EQUAL_UINT32(0, cueDurationMs(Cue::LongPressReached));
  TEST_ASSERT_EQUAL_UINT32(0, cueDurationMs(Cue::VeryLongPressReached));
  const Cue all[] = {Cue::Sent, Cue::SendFailed, Cue::PleaseWait, Cue::LongPressReached,
                     Cue::VeryLongPressReached, Cue::Muted, Cue::Unmuted};
  for (Cue cue : all) TEST_ASSERT_EQUAL_UINT32(cueDurationMs(cue), makeCue(cue)->durationMs());
}

static int countPeaks(Animation& a, uint32_t durationMs, uint8_t Rgb::*channel, uint8_t threshold) {
  int peaks = 0;
  bool above = false;
  Rgb px;
  for (uint32_t t = 0; t < durationMs; t += 5) {
    a.render(t, &px, 1);
    const bool now = px.*channel > threshold;
    if (now && !above) ++peaks;
    above = now;
  }
  return peaks;
}

static void test_sent_cue_is_two_green_pulses() {
  auto cue = makeCue(Cue::Sent);
  TEST_ASSERT_EQUAL_INT(2, countPeaks(*cue, 900, &Rgb::g, 100));
  const Rgb peak = maxOf(*cue, 900);
  TEST_ASSERT_EQUAL_UINT8(0, peak.r);
  TEST_ASSERT_TRUE(peak.g > 200);
  Rgb px;
  cue->render(0, &px, 1);
  TEST_ASSERT_TRUE(px.isBlack());
}

static void test_failed_cue_is_three_red_blinks() {
  auto cue = makeCue(Cue::SendFailed);
  TEST_ASSERT_EQUAL_INT(3, countPeaks(*cue, 900, &Rgb::r, 100));
  const Rgb peak = maxOf(*cue, 900);
  TEST_ASSERT_EQUAL_UINT8(0, peak.g);
  TEST_ASSERT_EQUAL_UINT8(0, peak.b);
}

static void test_please_wait_cue_is_one_dim_amber_pulse() {
  auto cue = makeCue(Cue::PleaseWait);
  TEST_ASSERT_EQUAL_INT(1, countPeaks(*cue, 600, &Rgb::r, 30));
  const Rgb peak = maxOf(*cue, 600);
  TEST_ASSERT_TRUE(peak.r > 30 && peak.r < 120);  // dim
  TEST_ASSERT_TRUE(peak.r > peak.g && peak.g > peak.b);  // amber
}

static void test_hold_cues_stay_until_cleared() {
  auto blue = makeCue(Cue::LongPressReached);
  Rgb a, b;
  blue->render(0, &a, 1);
  blue->render(60000, &b, 1);
  TEST_ASSERT_TRUE(a == b);
  TEST_ASSERT_TRUE(a.b > 200 && a.r == 0 && a.g == 0);

  auto chase = makeCue(Cue::VeryLongPressReached);
  std::vector<Rgb> f0(30), f1(30);
  chase->render(0, f0.data(), 30);
  chase->render(300, f1.data(), 30);
  TEST_ASSERT_TRUE(f0 != f1);  // it moves
  for (const Rgb& c : f0) TEST_ASSERT_EQUAL_UINT8(0, c.g);  // purple: red and blue only
  const Rgb peak = maxOf(*chase, 2000, 30);
  TEST_ASSERT_TRUE(peak.r > 100 && peak.b > 200);
}

static void test_muted_cue_fades_down_and_unmuted_fades_up() {
  auto muted = makeCue(Cue::Muted);
  Rgb px, previous(0, 0, 255);
  muted->render(0, &px, 1);
  TEST_ASSERT_TRUE(px.b >= 250);
  for (uint32_t t = 0; t < 800; t += 20) {
    muted->render(t, &px, 1);
    TEST_ASSERT_TRUE(px.b <= previous.b);
    TEST_ASSERT_EQUAL_UINT8(0, px.r);
    previous = px;
  }
  muted->render(800, &px, 1);
  TEST_ASSERT_TRUE(px.isBlack());

  auto unmuted = makeCue(Cue::Unmuted);
  uint8_t earlier = 0;
  for (uint32_t t = 0; t <= 580; t += 20) {
    unmuted->render(t, &px, 1);
    TEST_ASSERT_TRUE(px.b >= earlier);
    earlier = px.b;
  }
  TEST_ASSERT_TRUE(earlier > 200);
}

static void test_status_setup_is_slow_blue_breathing() {
  auto s = makeSetupIndication();
  const Rgb peak = maxOf(*s, 8000);
  TEST_ASSERT_TRUE(peak.b > 200);
  TEST_ASSERT_EQUAL_UINT8(0, peak.r);
  TEST_ASSERT_EQUAL_UINT8(0, peak.g);
  Rgb lo, hi;
  s->render(0, &lo, 1);
  s->render(2000, &hi, 1);
  TEST_ASSERT_TRUE(lo.b < 10 && hi.b > 200);
  TEST_ASSERT_EQUAL_UINT32(0, s->durationMs());
}

static void test_status_connecting_is_amber() {
  auto s = makeConnectingIndication();
  const Rgb peak = maxOf(*s, 5000);
  TEST_ASSERT_TRUE(peak.r > 100);
  TEST_ASSERT_TRUE(peak.r > peak.g);
  TEST_ASSERT_EQUAL_UINT8(0, peak.b);
}

static void test_lost_connection_lights_exactly_one_dim_red_led() {
  for (size_t n : {1u, 8u, 12u, 60u, 300u}) {
    auto s = makeConnectionLostIndication();
    std::vector<Rgb> px(n);
    int blinks = 0;
    bool previouslyLit = false;
    for (uint32_t t = 0; t < 21000; t += 10) {
      s->render(t, px.data(), n);
      const int lit = litCount(px);
      TEST_ASSERT_TRUE_MESSAGE(lit <= 1, "at most one LED is ever lit");
      if (lit == 1) {
        TEST_ASSERT_TRUE(px[0].r > 0 && px[0].g == 0 && px[0].b == 0);
        TEST_ASSERT_TRUE(px[0].r <= 100);  // dim
        if (!previouslyLit) ++blinks;
      }
      previouslyLit = lit == 1;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, blinks, "one blink about every 5 s");  // t = 0, 5, 10, 15, 20 s
    s->render(0, px.data(), n);
    TEST_ASSERT_EQUAL_INT(1, litCount(px));
    s->render(1000, px.data(), n);
    TEST_ASSERT_EQUAL_INT(0, litCount(px));
  }
}

static void test_update_progress_fill_is_proportional() {
  UpdateProgressIndication p;
  for (size_t n : {8u, 12u, 60u, 300u}) {
    for (int percent = 0; percent <= 100; percent += 5) {
      p.setPercent(percent);
      std::vector<Rgb> px(n);
      p.render(0, px.data(), n);
      const int lit = litCount(px);
      const double expect = percent * static_cast<double>(n) / 100.0;
      TEST_ASSERT_TRUE_MESSAGE(lit >= expect - 0.5 - 1e-9 && lit <= expect + 0.5 + 1e-9, "fill ~ percent * n");
      // The fill is contiguous from the start of the strip.
      for (int i = 0; i < lit; ++i) TEST_ASSERT_TRUE(!px[i].isBlack());
    }
  }
  p.setPercent(0);
  std::vector<Rgb> px(60);
  p.render(0, px.data(), 60);
  TEST_ASSERT_EQUAL_INT(0, litCount(px));
  p.setPercent(50);
  p.render(0, px.data(), 60);
  TEST_ASSERT_EQUAL_INT(30, litCount(px));
  p.setPercent(100);
  p.render(0, px.data(), 60);
  TEST_ASSERT_EQUAL_INT(60, litCount(px));
  p.setPercent(-20);
  TEST_ASSERT_EQUAL_INT(0, p.percent());
  p.setPercent(400);
  TEST_ASSERT_EQUAL_INT(100, p.percent());
  // A dim white fill, so it reads as progress and not as a colour cue.
  const Rgb c = px[0];
  TEST_ASSERT_TRUE(c.r == c.g && c.g == c.b && c.r < 100);
}

static void test_idle_is_everything_off() {
  Compositor c;
  c.setBrightness(100);
  c.setBase(makeConnectionLostIndication(), 0);
  c.setBase(nullptr, 10);  // connected and idle: no base animation
  TEST_ASSERT_EQUAL_INT(0, litCount(draw(c, 50, 60)));
}

// ---- 14.4 which indication shows ----

static IndicationInputs neverConnected(uint32_t uptimeMs) {
  IndicationInputs in;
  in.uptimeMs = uptimeMs;
  in.disconnectedForMs = uptimeMs;
  return in;
}

static void test_indication_after_boot_is_connecting_then_lost() {
  TEST_ASSERT_TRUE(chooseIndication(neverConnected(0)) == Indication::Connecting);
  TEST_ASSERT_TRUE(chooseIndication(neverConnected(59999)) == Indication::Connecting);
  TEST_ASSERT_TRUE_MESSAGE(chooseIndication(neverConnected(60001)) == Indication::ConnectionLost,
                           "a broker that never answered counts as lost after 60 s");
  TEST_ASSERT_EQUAL_STRING("connecting", indicationName(Indication::Connecting));
}

static void test_indication_when_connected_is_off() {
  IndicationInputs in = neverConnected(5000);
  in.brokerConnected = true;
  in.everConnected = true;
  in.disconnectedForMs = 0;
  TEST_ASSERT_TRUE(chooseIndication(in) == Indication::None);
  in.uptimeMs = 3600000;
  TEST_ASSERT_TRUE(chooseIndication(in) == Indication::None);
}

static void test_indication_after_a_drop_waits_60_seconds() {
  IndicationInputs in;
  in.everConnected = true;
  in.uptimeMs = 30000;  // still inside the first minute, but it has connected: no connecting pulse
  in.disconnectedForMs = 1000;
  TEST_ASSERT_TRUE(chooseIndication(in) == Indication::None);
  in.uptimeMs = 600000;
  in.disconnectedForMs = 60000;
  TEST_ASSERT_TRUE(chooseIndication(in) == Indication::None);
  in.disconnectedForMs = 60001;
  TEST_ASSERT_TRUE(chooseIndication(in) == Indication::ConnectionLost);
  in.brokerConnected = true;  // back again: off at once
  in.disconnectedForMs = 0;
  TEST_ASSERT_TRUE(chooseIndication(in) == Indication::None);
}

static void test_indication_setup_and_updating_outrank_the_broker() {
  IndicationInputs in = neverConnected(120000);
  in.portalOpen = true;
  TEST_ASSERT_TRUE(chooseIndication(in) == Indication::Setup);
  in.updating = true;
  TEST_ASSERT_TRUE(chooseIndication(in) == Indication::Updating);
  in.portalOpen = false;
  in.brokerConnected = true;
  TEST_ASSERT_TRUE(chooseIndication(in) == Indication::Updating);
}

void run_compositor_tests() {
  RUN_TEST(test_idle_compositor_is_dark);
  RUN_TEST(test_overlay_replacement_and_resumption);
  RUN_TEST(test_overlay_with_no_duration_lasts_until_cleared);
  RUN_TEST(test_overlay_covers_the_whole_base);
  RUN_TEST(test_fade_out_reaches_black_after_about_one_second);
  RUN_TEST(test_setting_a_new_base_cancels_a_fade);
  RUN_TEST(test_fade_with_no_base_does_nothing);
  RUN_TEST(test_brightness_scales_without_changing_hue);
  RUN_TEST(test_brightness_is_clamped_to_5_to_100);
  RUN_TEST(test_brightness_applies_to_overlays_too);
  RUN_TEST(test_render_writes_exactly_n_pixels);
  RUN_TEST(test_base_with_effect_keeps_its_own_timeline);
  RUN_TEST(test_cue_durations);
  RUN_TEST(test_sent_cue_is_two_green_pulses);
  RUN_TEST(test_failed_cue_is_three_red_blinks);
  RUN_TEST(test_please_wait_cue_is_one_dim_amber_pulse);
  RUN_TEST(test_hold_cues_stay_until_cleared);
  RUN_TEST(test_muted_cue_fades_down_and_unmuted_fades_up);
  RUN_TEST(test_status_setup_is_slow_blue_breathing);
  RUN_TEST(test_status_connecting_is_amber);
  RUN_TEST(test_lost_connection_lights_exactly_one_dim_red_led);
  RUN_TEST(test_update_progress_fill_is_proportional);
  RUN_TEST(test_idle_is_everything_off);
  RUN_TEST(test_indication_after_boot_is_connecting_then_lost);
  RUN_TEST(test_indication_when_connected_is_off);
  RUN_TEST(test_indication_after_a_drop_waits_60_seconds);
  RUN_TEST(test_indication_setup_and_updating_outrank_the_broker);
}
