// Tests for button_gestures (5.1), cooldown/controls (5.2), quiet_hours (5.3) and backoff (5.4).
#include <unity.h>

#include <string>
#include <vector>

#include "backoff.h"
#include "button_gestures.h"
#include "cooldown.h"
#include "quiet_hours.h"

using namespace yule;

namespace {

struct Timed {
  uint32_t atMs;
  GestureEvent event;
};

// Drives a gesture recogniser with a scripted raw button signal, one sample per millisecond.
class Rig {
 public:
  // Holds `pressed` from the current time until `untilMs` (exclusive).
  void hold(bool pressed, uint32_t untilMs) {
    for (; now_ < untilMs; ++now_) {
      g_.update(now_, pressed);
      drain();
    }
  }
  // Raw samples toggling every `periodMs` for `durationMs`, ending at `finalLevel`.
  void bounce(uint32_t durationMs, uint32_t periodMs, bool startLevel, bool finalLevel) {
    const uint32_t end = now_ + durationMs;
    bool level = startLevel;
    uint32_t next = now_ + periodMs;
    for (; now_ < end; ++now_) {
      if (now_ >= next) {
        level = !level;
        next += periodMs;
      }
      g_.update(now_, level);
      drain();
    }
    g_.update(now_, finalLevel);
    drain();
  }
  uint32_t now() const { return now_; }
  const std::vector<Timed>& events() const { return events_; }
  std::vector<GestureEvent> kinds() const {
    std::vector<GestureEvent> v;
    for (const Timed& t : events_) v.push_back(t.event);
    return v;
  }
  bool has(GestureEvent e) const {
    for (const Timed& t : events_) {
      if (t.event == e) return true;
    }
    return false;
  }
  int count(GestureEvent e) const {
    int n = 0;
    for (const Timed& t : events_) n += t.event == e;
    return n;
  }
  uint32_t timeOf(GestureEvent e) const {
    for (const Timed& t : events_) {
      if (t.event == e) return t.atMs;
    }
    return 0xFFFFFFFFu;
  }
  ButtonGestures& gestures() { return g_; }

 private:
  void drain() {
    GestureEvent e;
    while (g_.poll(e)) events_.push_back({now_, e});
  }
  ButtonGestures g_;
  uint32_t now_ = 0;
  std::vector<Timed> events_;
};

// A press of `heldMs` after 100 ms of rest, followed by 200 ms of rest to let the release settle.
Rig press(uint32_t heldMs) {
  Rig r;
  r.hold(false, 100);
  r.hold(true, 100 + heldMs);
  r.hold(false, 100 + heldMs + 200);
  return r;
}

}  // namespace

// ---- 5.1 button_gestures ----

static void test_a_tap_is_one_short_press() {
  const Rig r = press(120);
  TEST_ASSERT_EQUAL_INT(1, r.count(GestureEvent::ShortPress));
  TEST_ASSERT_EQUAL_INT(0, r.count(GestureEvent::LongPress));
  TEST_ASSERT_EQUAL_INT(0, r.count(GestureEvent::LongThresholdReached));
  TEST_ASSERT_EQUAL_INT(1, r.count(GestureEvent::Released));
  // The action happens on release, not on press.
  TEST_ASSERT_TRUE(r.timeOf(GestureEvent::ShortPress) >= 100 + 120);
}

static void test_a_hesitant_1_5_second_hold_does_nothing() {
  const Rig r = press(1500);
  TEST_ASSERT_EQUAL_INT(0, r.count(GestureEvent::ShortPress));
  TEST_ASSERT_EQUAL_INT(0, r.count(GestureEvent::LongPress));
  TEST_ASSERT_EQUAL_INT(0, r.count(GestureEvent::LongThresholdReached));
  TEST_ASSERT_EQUAL_INT(0, r.count(GestureEvent::VeryLongThresholdReached));
  TEST_ASSERT_EQUAL_INT(1, r.count(GestureEvent::Released));
}

static void test_20ms_of_bounce_gives_a_single_short_press() {
  Rig r;
  r.hold(false, 100);
  r.bounce(20, 4, true, true);  // press edge, then contacts chatter for 20 ms
  r.hold(true, 220);            // a firm press
  r.bounce(20, 4, false, false);  // release chatter
  r.hold(false, 500);
  TEST_ASSERT_EQUAL_INT(1, r.count(GestureEvent::ShortPress));
  TEST_ASSERT_EQUAL_INT(1, r.count(GestureEvent::Released));
  TEST_ASSERT_EQUAL_INT(0, r.count(GestureEvent::LongPress));
}

static void test_a_glitch_shorter_than_the_debounce_time_is_ignored() {
  Rig r;
  r.hold(false, 100);
  r.hold(true, 120);  // 20 ms blip
  r.hold(false, 500);
  TEST_ASSERT_EQUAL_UINT32(0, r.events().size());
  TEST_ASSERT_FALSE(r.gestures().held());
}

static void test_a_three_second_hold_is_a_long_press() {
  const Rig r = press(3000);
  TEST_ASSERT_EQUAL_INT(1, r.count(GestureEvent::LongThresholdReached));
  TEST_ASSERT_EQUAL_INT(1, r.count(GestureEvent::LongPress));
  TEST_ASSERT_EQUAL_INT(0, r.count(GestureEvent::ShortPress));
  TEST_ASSERT_EQUAL_INT(0, r.count(GestureEvent::VeryLongThresholdReached));
  // The cue appears when 2 s is reached while the button is still down; the action is on release.
  TEST_ASSERT_UINT32_WITHIN(5, 100 + 2000, r.timeOf(GestureEvent::LongThresholdReached));
  TEST_ASSERT_TRUE(r.timeOf(GestureEvent::LongPress) >= 100 + 3000);
  // Order: threshold, long press, released.
  const auto k = r.kinds();
  TEST_ASSERT_TRUE(k[0] == GestureEvent::LongThresholdReached);
  TEST_ASSERT_TRUE(k[1] == GestureEvent::LongPress);
  TEST_ASSERT_TRUE(k[2] == GestureEvent::Released);
}

static void test_a_twelve_second_hold_fires_very_long_at_ten_seconds_and_nothing_on_release() {
  const Rig r = press(12000);
  TEST_ASSERT_EQUAL_INT(1, r.count(GestureEvent::LongThresholdReached));
  TEST_ASSERT_EQUAL_INT(1, r.count(GestureEvent::VeryLongThresholdReached));
  TEST_ASSERT_UINT32_WITHIN(5, 100 + 10000, r.timeOf(GestureEvent::VeryLongThresholdReached));
  TEST_ASSERT_TRUE_MESSAGE(r.timeOf(GestureEvent::VeryLongThresholdReached) < 100 + 12000, "acts before the release");
  TEST_ASSERT_EQUAL_INT(0, r.count(GestureEvent::LongPress));
  TEST_ASSERT_EQUAL_INT(0, r.count(GestureEvent::ShortPress));
  TEST_ASSERT_EQUAL_INT(1, r.count(GestureEvent::Released));
}

static void test_threshold_boundaries() {
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, press(999).count(GestureEvent::ShortPress), "999 ms short");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, press(1000).count(GestureEvent::ShortPress), "1000 ms is not short");  // 1 s: nothing
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, press(1000).count(GestureEvent::LongPress), "1000 ms is not long");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, press(1999).count(GestureEvent::LongPress), "1999 ms is not long");  // just under 2 s
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, press(2000).count(GestureEvent::LongPress), "2000 ms long");        // 2 s: long press
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, press(9999).count(GestureEvent::LongPress), "9999 ms long");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, press(9999).count(GestureEvent::VeryLongThresholdReached), "9999 ms not very long");
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, press(10000).count(GestureEvent::VeryLongThresholdReached), "10000 ms very long");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, press(10000).count(GestureEvent::LongPress), "10000 ms is not a long press");
}

static void test_held_reports_while_the_button_is_down() {
  Rig r;
  r.hold(false, 100);
  TEST_ASSERT_FALSE(r.gestures().held());
  r.hold(true, 500);
  TEST_ASSERT_TRUE(r.gestures().held());
  r.hold(false, 700);
  TEST_ASSERT_FALSE(r.gestures().held());
}

static void test_two_presses_in_a_row_each_register() {
  Rig r;
  r.hold(false, 100);
  r.hold(true, 200);
  r.hold(false, 600);
  r.hold(true, 700);
  r.hold(false, 1200);
  TEST_ASSERT_EQUAL_INT(2, r.count(GestureEvent::ShortPress));
}

static void test_a_late_poll_loop_still_classifies_correctly() {
  // Sampling every 100 ms instead of every 1 ms still gives thresholds and one release.
  ButtonGestures g;
  GestureEvent e;
  std::vector<GestureEvent> got;
  uint32_t t = 0;
  for (; t < 300; t += 100) g.update(t, false);
  for (; t < 3300; t += 100) {
    g.update(t, true);
    while (g.poll(e)) got.push_back(e);
  }
  for (; t < 3800; t += 100) {
    g.update(t, false);
    while (g.poll(e)) got.push_back(e);
  }
  TEST_ASSERT_EQUAL_UINT32(3, got.size());
  TEST_ASSERT_TRUE(got[0] == GestureEvent::LongThresholdReached);
  TEST_ASSERT_TRUE(got[1] == GestureEvent::LongPress);
  TEST_ASSERT_TRUE(got[2] == GestureEvent::Released);
}

static void test_gestures_survive_millis_wraparound() {
  ButtonGestures g;
  GestureEvent e;
  std::vector<GestureEvent> got;
  uint32_t t = 0xFFFFFF00u;
  for (int i = 0; i < 100; ++i, ++t) g.update(t, false);
  for (int i = 0; i < 500; ++i, ++t) {
    g.update(t, true);
    while (g.poll(e)) got.push_back(e);
  }
  for (int i = 0; i < 100; ++i, ++t) {
    g.update(t, false);
    while (g.poll(e)) got.push_back(e);
  }
  TEST_ASSERT_EQUAL_UINT32(2, got.size());
  TEST_ASSERT_TRUE(got[0] == GestureEvent::ShortPress);
  TEST_ASSERT_TRUE(got[1] == GestureEvent::Released);
}

// ---- 5.2 cooldown and controls ----

static void test_cooldown_basics() {
  Cooldown c(30000);
  TEST_ASSERT_FALSE(c.active(0));
  c.start(1000);
  TEST_ASSERT_TRUE(c.active(1000));
  TEST_ASSERT_TRUE(c.active(30999));
  TEST_ASSERT_EQUAL_UINT32(1, c.remainingMs(30999));
  TEST_ASSERT_FALSE(c.active(31000));
  TEST_ASSERT_EQUAL_UINT32(0, c.remainingMs(31000));
  c.setDurationMs(5000);
  c.start(100000);
  TEST_ASSERT_TRUE(c.active(104999));
  TEST_ASSERT_FALSE(c.active(105000));
  Cooldown disabled(0);
  disabled.start(5);
  TEST_ASSERT_FALSE(disabled.active(5));
  TEST_ASSERT_EQUAL_UINT32(30000, Cooldown().durationMs());  // the default
}

static void test_ten_presses_in_five_seconds_send_one_and_show_please_wait_nine_times() {
  ControlLogic c(30000);
  int sends = 0, waits = 0;
  for (int i = 0; i < 10; ++i) {
    const uint32_t t = i * 500;
    const ControlAction a = c.onGesture(GestureEvent::ShortPress, t);
    if (a == ControlAction::SendPress) {
      ++sends;
      c.onSendResult(true, t + 80);  // acknowledged almost at once
    } else if (a == ControlAction::PleaseWait) {
      ++waits;
    }
  }
  TEST_ASSERT_EQUAL_INT(1, sends);
  TEST_ASSERT_EQUAL_INT(9, waits);
}

static void test_presses_while_a_send_is_in_flight_do_not_double_send() {
  ControlLogic c(30000);
  TEST_ASSERT_TRUE(c.onGesture(GestureEvent::ShortPress, 0) == ControlAction::SendPress);
  TEST_ASSERT_TRUE(c.sendInFlight());
  TEST_ASSERT_TRUE(c.onGesture(GestureEvent::ShortPress, 300) == ControlAction::PleaseWait);
  c.onSendResult(true, 400);
  TEST_ASSERT_FALSE(c.sendInFlight());
  TEST_ASSERT_TRUE(c.onGesture(GestureEvent::ShortPress, 500) == ControlAction::PleaseWait);
}

static void test_a_failed_send_does_not_start_the_cooldown() {
  ControlLogic c(30000);
  TEST_ASSERT_TRUE(c.onGesture(GestureEvent::ShortPress, 0) == ControlAction::SendPress);
  c.onSendResult(false, 10);
  TEST_ASSERT_FALSE(c.cooldown().active(11));
  TEST_ASSERT_TRUE_MESSAGE(c.onGesture(GestureEvent::ShortPress, 20) == ControlAction::SendPress, "retry straight away");
  c.onSendResult(true, 120);
  TEST_ASSERT_TRUE(c.onGesture(GestureEvent::ShortPress, 130) == ControlAction::PleaseWait);
}

static void test_cooldown_ends_after_thirty_seconds() {
  ControlLogic c(30000);
  c.onGesture(GestureEvent::ShortPress, 0);
  c.onSendResult(true, 100);
  TEST_ASSERT_TRUE(c.onGesture(GestureEvent::ShortPress, 30099) == ControlAction::PleaseWait);
  TEST_ASSERT_TRUE(c.onGesture(GestureEvent::ShortPress, 30100) == ControlAction::SendPress);
}

static void test_long_and_very_long_presses_ignore_the_cooldown() {
  ControlLogic c(30000);
  c.onGesture(GestureEvent::ShortPress, 0);
  c.onSendResult(true, 50);
  TEST_ASSERT_TRUE(c.cooldown().active(1000));
  TEST_ASSERT_TRUE(c.onGesture(GestureEvent::LongPress, 1000) == ControlAction::ToggleMute);
  TEST_ASSERT_TRUE(c.onGesture(GestureEvent::LongThresholdReached, 1000) == ControlAction::ShowLongCue);
  TEST_ASSERT_TRUE(c.onGesture(GestureEvent::VeryLongThresholdReached, 1000) == ControlAction::OpenWifiSetup);
  TEST_ASSERT_TRUE(c.onGesture(GestureEvent::Released, 1000) == ControlAction::ClearHoldCue);
  TEST_ASSERT_TRUE(c.onGesture(GestureEvent::None, 1000) == ControlAction::None);
}

static void test_gestures_and_controls_work_together_for_a_real_tap_and_hold() {
  Rig r = press(150);
  ControlLogic c(30000);
  bool sent = false;
  for (const Timed& t : r.events()) {
    if (c.onGesture(t.event, t.atMs) == ControlAction::SendPress) sent = true;
  }
  TEST_ASSERT_TRUE(sent);
}

// ---- 5.3 quiet_hours ----

namespace {
QuietHours nightly() {
  QuietHours q;
  q.enabled = true;
  q.startMinute = 21 * 60 + 30;
  q.endMinute = 7 * 60;
  return q;
}
}  // namespace

static void test_quiet_hours_across_midnight() {
  const QuietHours q = nightly();
  TEST_ASSERT_TRUE(inQuietHours(q, true, 23 * 60 + 15));   // 23:15: quiet
  TEST_ASSERT_FALSE(inQuietHours(q, true, 7 * 60));         // 07:00: the end is exclusive
  TEST_ASSERT_TRUE(inQuietHours(q, true, 6 * 60 + 59));
  TEST_ASSERT_TRUE(inQuietHours(q, true, 21 * 60 + 30));    // the start is inclusive
  TEST_ASSERT_FALSE(inQuietHours(q, true, 21 * 60 + 29));
  TEST_ASSERT_TRUE(inQuietHours(q, true, 0));               // midnight
  TEST_ASSERT_TRUE(inQuietHours(q, true, 3 * 60));
  TEST_ASSERT_FALSE(inQuietHours(q, true, 12 * 60));
}

static void test_quiet_hours_within_one_day() {
  QuietHours q;
  q.enabled = true;
  q.startMinute = 13 * 60;
  q.endMinute = 15 * 60;
  TEST_ASSERT_FALSE(inQuietHours(q, true, 12 * 60 + 59));
  TEST_ASSERT_TRUE(inQuietHours(q, true, 13 * 60));
  TEST_ASSERT_TRUE(inQuietHours(q, true, 14 * 60 + 59));
  TEST_ASSERT_FALSE(inQuietHours(q, true, 15 * 60));
  TEST_ASSERT_FALSE(inQuietHours(q, true, 23 * 60));
}

static void test_equal_start_and_end_means_no_quiet_hours() {
  QuietHours q;
  q.enabled = true;
  q.startMinute = q.endMinute = 22 * 60;
  for (int m = 0; m < kMinutesPerDay; m += 7) TEST_ASSERT_FALSE(inQuietHours(q, true, m));
}

static void test_disabled_quiet_hours_never_apply() {
  QuietHours q = nightly();
  q.enabled = false;
  for (int m = 0; m < kMinutesPerDay; m += 7) TEST_ASSERT_FALSE(inQuietHours(q, true, m));
}

static void test_quiet_hours_never_apply_before_the_clock_is_synchronised() {
  const QuietHours q = nightly();
  TEST_ASSERT_FALSE(inQuietHours(q, false, 23 * 60 + 15));
  SoundContext ctx;
  ctx.quiet = q;
  ctx.clockSynced = false;
  ctx.minuteOfDay = 23 * 60 + 15;
  TEST_ASSERT_TRUE(soundAllowed(ctx));
}

static void test_sound_permission_rule() {
  SoundContext ctx;
  ctx.quiet = nightly();
  ctx.clockSynced = true;
  ctx.minuteOfDay = 12 * 60;
  TEST_ASSERT_TRUE(soundAllowed(ctx));  // daytime, not muted
  ctx.minuteOfDay = 23 * 60 + 15;
  TEST_ASSERT_FALSE(soundAllowed(ctx));  // quiet hours
  ctx.minuteOfDay = 7 * 60;
  TEST_ASSERT_TRUE(soundAllowed(ctx));  // morning message plays its song
  ctx.muted = true;
  TEST_ASSERT_FALSE(soundAllowed(ctx));  // muted wins at any time of day
  ctx.minuteOfDay = 12 * 60;
  TEST_ASSERT_FALSE(soundAllowed(ctx));
}

static void test_a_preview_sounds_even_when_muted_or_in_quiet_hours() {
  SoundContext ctx;
  ctx.quiet = nightly();
  ctx.clockSynced = true;
  ctx.minuteOfDay = 23 * 60;
  ctx.muted = true;
  ctx.preview = true;
  TEST_ASSERT_TRUE(soundAllowed(ctx));
  ctx.preview = false;
  TEST_ASSERT_FALSE(soundAllowed(ctx));
}

static void test_clock_time_parsing_and_formatting() {
  TEST_ASSERT_EQUAL_INT(0, parseClockTime("00:00"));
  TEST_ASSERT_EQUAL_INT(21 * 60 + 30, parseClockTime("21:30"));
  TEST_ASSERT_EQUAL_INT(23 * 60 + 59, parseClockTime("23:59"));
  TEST_ASSERT_EQUAL_INT(7 * 60, parseClockTime("07:00"));
  const char* bad[] = {"", "7:00", "24:00", "12:60", "12-30", "ab:cd", "12:3", "12:300", " 7:00", "07:00 "};
  for (const char* b : bad) TEST_ASSERT_EQUAL_INT_MESSAGE(-1, parseClockTime(b), b);
  TEST_ASSERT_EQUAL_STRING("21:30", formatClockTime(21 * 60 + 30).c_str());
  TEST_ASSERT_EQUAL_STRING("07:05", formatClockTime(7 * 60 + 5).c_str());
  TEST_ASSERT_EQUAL_STRING("00:00", formatClockTime(0).c_str());
  for (int m = 0; m < kMinutesPerDay; m += 13) TEST_ASSERT_EQUAL_INT(m, parseClockTime(formatClockTime(m)));
}

// ---- 5.4 backoff ----

static void test_backoff_grows_and_never_exceeds_60_seconds() {
  Backoff b(1000, 60000, 5);
  uint32_t previous = 0;
  for (int i = 0; i < 40; ++i) {
    const uint32_t d = b.nextDelayMs();
    TEST_ASSERT_TRUE_MESSAGE(d <= 60000, "delay must be capped at 60 s");
    TEST_ASSERT_TRUE(d >= 750);
    if (i < 6) TEST_ASSERT_TRUE_MESSAGE(d > previous, "delays grow until the cap");
    previous = d;
  }
  TEST_ASSERT_TRUE(previous >= 45000);  // sits near the cap
  TEST_ASSERT_EQUAL_UINT32(40, b.attempts());
}

static void test_backoff_ceilings_double_then_cap() {
  Backoff b(1000, 60000);
  TEST_ASSERT_EQUAL_UINT32(1000, b.ceilingMs(0));
  TEST_ASSERT_EQUAL_UINT32(2000, b.ceilingMs(1));
  TEST_ASSERT_EQUAL_UINT32(4000, b.ceilingMs(2));
  TEST_ASSERT_EQUAL_UINT32(32000, b.ceilingMs(5));
  TEST_ASSERT_EQUAL_UINT32(60000, b.ceilingMs(6));
  TEST_ASSERT_EQUAL_UINT32(60000, b.ceilingMs(100));
}

static void test_backoff_resets_after_success() {
  Backoff b(1000, 60000, 3);
  for (int i = 0; i < 10; ++i) b.nextDelayMs();
  b.reset();
  TEST_ASSERT_EQUAL_UINT32(0, b.attempts());
  const uint32_t d = b.nextDelayMs();
  TEST_ASSERT_TRUE(d >= 750 && d <= 1000);
}

static void test_backoff_has_jitter() {
  uint32_t distinct = 0;
  uint32_t first = 0;
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    Backoff b(1000, 60000, seed);
    for (int i = 0; i < 5; ++i) b.nextDelayMs();
    const uint32_t d = b.nextDelayMs();  // 6th: ceiling 32 s
    if (seed == 1) first = d;
    distinct += d != first;
  }
  TEST_ASSERT_TRUE(distinct > 5);
}

void run_controls_tests() {
  RUN_TEST(test_a_tap_is_one_short_press);
  RUN_TEST(test_a_hesitant_1_5_second_hold_does_nothing);
  RUN_TEST(test_20ms_of_bounce_gives_a_single_short_press);
  RUN_TEST(test_a_glitch_shorter_than_the_debounce_time_is_ignored);
  RUN_TEST(test_a_three_second_hold_is_a_long_press);
  RUN_TEST(test_a_twelve_second_hold_fires_very_long_at_ten_seconds_and_nothing_on_release);
  RUN_TEST(test_threshold_boundaries);
  RUN_TEST(test_held_reports_while_the_button_is_down);
  RUN_TEST(test_two_presses_in_a_row_each_register);
  RUN_TEST(test_a_late_poll_loop_still_classifies_correctly);
  RUN_TEST(test_gestures_survive_millis_wraparound);
  RUN_TEST(test_cooldown_basics);
  RUN_TEST(test_ten_presses_in_five_seconds_send_one_and_show_please_wait_nine_times);
  RUN_TEST(test_presses_while_a_send_is_in_flight_do_not_double_send);
  RUN_TEST(test_a_failed_send_does_not_start_the_cooldown);
  RUN_TEST(test_cooldown_ends_after_thirty_seconds);
  RUN_TEST(test_long_and_very_long_presses_ignore_the_cooldown);
  RUN_TEST(test_gestures_and_controls_work_together_for_a_real_tap_and_hold);
  RUN_TEST(test_quiet_hours_across_midnight);
  RUN_TEST(test_quiet_hours_within_one_day);
  RUN_TEST(test_equal_start_and_end_means_no_quiet_hours);
  RUN_TEST(test_disabled_quiet_hours_never_apply);
  RUN_TEST(test_quiet_hours_never_apply_before_the_clock_is_synchronised);
  RUN_TEST(test_sound_permission_rule);
  RUN_TEST(test_a_preview_sounds_even_when_muted_or_in_quiet_hours);
  RUN_TEST(test_clock_time_parsing_and_formatting);
  RUN_TEST(test_backoff_grows_and_never_exceeds_60_seconds);
  RUN_TEST(test_backoff_ceilings_double_then_cap);
  RUN_TEST(test_backoff_resets_after_success);
  RUN_TEST(test_backoff_has_jitter);
}
