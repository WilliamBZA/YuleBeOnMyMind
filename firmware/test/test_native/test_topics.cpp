// Tests for topics and the receive filters (4.2) and playback_queue (4.3).
#include <unity.h>

#include <string>

#include "playback_queue.h"
#include "press_message.h"
#include "rtttl.h"
#include "topics.h"

using namespace yule;

namespace {

const char* kOwn = "d-aaaaaaaaaaaa";
const char* kGran = "d-bbbbbbbbbbbb";
const char* kBob = "d-cccccccccccc";
const char* kG1 = "g-aaaaaaaa";
const char* kG2 = "g-bbbbbbbb";
const char* kG3 = "g-cccccccc";
constexpr int64_t kNow = 1797000000;

Membership twoGroups() {
  Membership m;
  m.groups.push_back({kG1, "One"});
  m.groups.push_back({kG2, "Two"});
  return m;
}

std::string payload(const std::string& id, const std::string& from, int64_t ts) {
  PressMessage m;
  m.id = id;
  m.from = from;
  m.name = "Sender";
  m.ts = ts;
  m.song = "T:d=4,o=5,b=120:c,e,g";
  return encodePress(m);
}

std::string pressTopic(const std::string& group, const std::string& sender) {
  return Topics().press(group, sender);
}

PressReceiver receiver() { return PressReceiver(Topics(), kOwn); }

}  // namespace

// ---- topics ----

static void test_topic_builders() {
  const Topics t;
  TEST_ASSERT_EQUAL_STRING("yule/v1/groups/g-7f3k2abc/press/d-3c71bf4a2b10", t.press("g-7f3k2abc", "d-3c71bf4a2b10").c_str());
  TEST_ASSERT_EQUAL_STRING("yule/v1/groups/g-7f3k2abc/press/+", t.pressAllSenders("g-7f3k2abc").c_str());
  TEST_ASSERT_EQUAL_STRING("yule/v1/devices/d-3c71bf4a2b10/config", t.config("d-3c71bf4a2b10").c_str());
  TEST_ASSERT_EQUAL_STRING("yule/v1/devices/d-3c71bf4a2b10/status", t.status("d-3c71bf4a2b10").c_str());
  TEST_ASSERT_EQUAL_STRING("yule/v1/firmware/beta/esp32-c3", t.manifest("beta", "esp32-c3").c_str());
  TEST_ASSERT_EQUAL_STRING("test/x/groups/g-aaaaaaaa/press/d-bbbbbbbbbbbb", Topics("test/x").press(kG1, kGran).c_str());
}

static void test_press_topic_parsing() {
  const Topics t;
  Topics::PressTopic p;
  TEST_ASSERT_TRUE(t.parsePress("yule/v1/groups/g-7f3k2abc/press/d-3c71bf4a2b10", p));
  TEST_ASSERT_EQUAL_STRING("g-7f3k2abc", p.group.c_str());
  TEST_ASSERT_EQUAL_STRING("d-3c71bf4a2b10", p.sender.c_str());
  // Round trip.
  TEST_ASSERT_TRUE(t.parsePress(t.press(kG1, kGran), p));
  TEST_ASSERT_EQUAL_STRING(kG1, p.group.c_str());
  const char* bad[] = {"",
                       "yule/v1",
                       "yule/v1/",
                       "yule/v2/groups/g-7f3k2abc/press/d-3c71bf4a2b10",
                       "other/groups/g-7f3k2abc/press/d-3c71bf4a2b10",
                       "yule/v1/groups/g-7f3k2abc/press/+",
                       "yule/v1/groups/+/press/d-3c71bf4a2b10",
                       "yule/v1/groups/#",
                       "yule/v1/groups/g-7f3k2abc/press",
                       "yule/v1/groups/g-7f3k2abc/press/d-3c71bf4a2b10/extra",
                       "yule/v1/groups/g-7f3k2abc/press/d-3c71bf4a2b",
                       "yule/v1/groups/g-7f3k2ab/press/d-3c71bf4a2b10",
                       "yule/v1/groups/g-7f3k2abc/pressX/d-3c71bf4a2b10",
                       "yule/v1/devices/d-3c71bf4a2b10/config",
                       "yule/v1/groups//press/d-3c71bf4a2b10",
                       "/yule/v1/groups/g-7f3k2abc/press/d-3c71bf4a2b10"};
  for (const char* b : bad) TEST_ASSERT_FALSE_MESSAGE(t.parsePress(b, p), b);
}

static void test_drop_reason_names() {
  TEST_ASSERT_EQUAL_STRING("duplicate", dropReasonName(DropReason::Duplicate));
  TEST_ASSERT_EQUAL_STRING("stale", dropReasonName(DropReason::Stale));
  TEST_ASSERT_EQUAL_STRING("forged", dropReasonName(DropReason::Forged));
  TEST_ASSERT_EQUAL_STRING("own", dropReasonName(DropReason::Own));
  TEST_ASSERT_EQUAL_STRING("not-a-member", dropReasonName(DropReason::NotMember));
  TEST_ASSERT_EQUAL_STRING("malformed", dropReasonName(DropReason::Malformed));
  TEST_ASSERT_EQUAL_STRING("bad-topic", dropReasonName(DropReason::BadTopic));
}

// ---- receive filters ----

static void test_a_valid_press_is_accepted() {
  PressReceiver r = receiver();
  const auto res = r.receive(pressTopic(kG1, kGran), payload("m1", kGran, kNow - 5), twoGroups(), kNow);
  TEST_ASSERT_TRUE(res.accepted());
  TEST_ASSERT_EQUAL_STRING("m1", res.message.id.c_str());
  TEST_ASSERT_EQUAL_STRING(kG1, res.group.c_str());
  TEST_ASSERT_EQUAL_STRING(kOwn, r.ownId().c_str());
}

static void test_duplicate_delivery_plays_once() {
  PressReceiver r = receiver();
  const std::string topic = pressTopic(kG1, kGran);
  TEST_ASSERT_TRUE(r.receive(topic, payload("dup1", kGran, kNow), twoGroups(), kNow).accepted());
  TEST_ASSERT_TRUE(r.receive(topic, payload("dup1", kGran, kNow), twoGroups(), kNow).drop == DropReason::Duplicate);
}

static void test_same_press_through_two_shared_groups_plays_once() {
  PressReceiver r = receiver();
  const std::string body = payload("shared1", kGran, kNow);
  TEST_ASSERT_TRUE(r.receive(pressTopic(kG1, kGran), body, twoGroups(), kNow).accepted());
  TEST_ASSERT_TRUE(r.receive(pressTopic(kG2, kGran), body, twoGroups(), kNow).drop == DropReason::Duplicate);
  // A different press afterwards is still accepted.
  TEST_ASSERT_TRUE(r.receive(pressTopic(kG2, kGran), payload("shared2", kGran, kNow), twoGroups(), kNow).accepted());
}

static void test_forged_sender_is_dropped() {
  PressReceiver r = receiver();
  // Arrives on Bob's topic but claims to be from Gran.
  const auto res = r.receive(pressTopic(kG1, kBob), payload("f1", kGran, kNow), twoGroups(), kNow);
  TEST_ASSERT_TRUE(res.drop == DropReason::Forged);
  // A forged press must not poison de-duplication: the genuine one with the same id still plays.
  TEST_ASSERT_TRUE(r.receive(pressTopic(kG1, kGran), payload("f1", kGran, kNow), twoGroups(), kNow).accepted());
}

static void test_own_press_is_ignored() {
  PressReceiver r = receiver();
  const auto res = r.receive(pressTopic(kG1, kOwn), payload("own1", kOwn, kNow), twoGroups(), kNow);
  TEST_ASSERT_TRUE(res.drop == DropReason::Own);
}

static void test_press_from_a_group_we_are_not_in_is_dropped() {
  PressReceiver r = receiver();
  const auto res = r.receive(pressTopic(kG3, kGran), payload("nm1", kGran, kNow), twoGroups(), kNow);
  TEST_ASSERT_TRUE(res.drop == DropReason::NotMember);
  // With no groups at all, nothing is accepted.
  TEST_ASSERT_TRUE(r.receive(pressTopic(kG1, kGran), payload("nm2", kGran, kNow), Membership(), kNow).drop == DropReason::NotMember);
}

static void test_stale_presses_are_dropped_but_recent_and_unknown_time_are_played() {
  PressReceiver r = receiver();
  const std::string topic = pressTopic(kG1, kGran);
  TEST_ASSERT_TRUE(r.receive(topic, payload("old", kGran, kNow - 8 * 3600), twoGroups(), kNow).drop == DropReason::Stale);
  TEST_ASSERT_TRUE(r.receive(topic, payload("recent", kGran, kNow - 30), twoGroups(), kNow).accepted());
  TEST_ASSERT_TRUE_MESSAGE(r.receive(topic, payload("notime", kGran, 0), twoGroups(), kNow).accepted(), "unknown send time plays");
  TEST_ASSERT_TRUE_MESSAGE(r.receive(topic, payload("noclock", kGran, kNow - 8 * 3600), twoGroups(), 0).accepted(),
                           "unknown local clock plays");
  TEST_ASSERT_TRUE_MESSAGE(r.receive(topic, payload("edge300", kGran, kNow - 300), twoGroups(), kNow).accepted(), "300 s is not yet stale");
  TEST_ASSERT_TRUE(r.receive(topic, payload("edge301", kGran, kNow - 301), twoGroups(), kNow).drop == DropReason::Stale);
  TEST_ASSERT_TRUE_MESSAGE(r.receive(topic, payload("future", kGran, kNow + 120), twoGroups(), kNow).accepted(),
                           "a sender clock that is ahead counts as fresh");
}

static void test_malformed_payloads_and_topics_are_dropped() {
  PressReceiver r = receiver();
  TEST_ASSERT_TRUE(r.receive(pressTopic(kG1, kGran), "garbage", twoGroups(), kNow).drop == DropReason::Malformed);
  TEST_ASSERT_TRUE(r.receive(pressTopic(kG1, kGran), "", twoGroups(), kNow).drop == DropReason::Malformed);
  TEST_ASSERT_TRUE(r.receive(pressTopic(kG1, kGran), "{\"v\":1,\"id\":\"x\"}", twoGroups(), kNow).drop == DropReason::Malformed);
  TEST_ASSERT_TRUE(r.receive("yule/v1/devices/d-bbbbbbbbbbbb/config", payload("x", kGran, kNow), twoGroups(), kNow).drop == DropReason::BadTopic);
  TEST_ASSERT_TRUE(r.receive("", "x", twoGroups(), kNow).drop == DropReason::BadTopic);
}

static void test_filters_apply_in_the_documented_order() {
  PressReceiver r = receiver();
  // Topic is checked before membership, membership before the payload.
  TEST_ASSERT_TRUE(r.receive("nonsense", "garbage", twoGroups(), kNow).drop == DropReason::BadTopic);
  TEST_ASSERT_TRUE(r.receive(pressTopic(kG3, kGran), "garbage", twoGroups(), kNow).drop == DropReason::NotMember);
  // Forged is decided before own: payload says "own" but the topic says Gran.
  TEST_ASSERT_TRUE(r.receive(pressTopic(kG1, kGran), payload("o1", kOwn, kNow), twoGroups(), kNow).drop == DropReason::Forged);
  // Duplicate is decided before stale.
  TEST_ASSERT_TRUE(r.receive(pressTopic(kG1, kGran), payload("d1", kGran, kNow), twoGroups(), kNow).accepted());
  TEST_ASSERT_TRUE(r.receive(pressTopic(kG1, kGran), payload("d1", kGran, kNow - 9999), twoGroups(), kNow).drop == DropReason::Duplicate);
}

static void test_message_id_ring_remembers_the_last_32() {
  PressReceiver r = receiver();
  const std::string topic = pressTopic(kG1, kGran);
  for (int i = 0; i < 32; ++i) TEST_ASSERT_TRUE(r.receive(topic, payload("id" + std::to_string(i), kGran, kNow), twoGroups(), kNow).accepted());
  TEST_ASSERT_TRUE(r.receive(topic, payload("id0", kGran, kNow), twoGroups(), kNow).drop == DropReason::Duplicate);
  TEST_ASSERT_TRUE(r.receive(topic, payload("id31", kGran, kNow), twoGroups(), kNow).drop == DropReason::Duplicate);
  // A 33rd new id pushes the oldest out of the ring, so it would play again.
  TEST_ASSERT_TRUE(r.receive(topic, payload("id32", kGran, kNow), twoGroups(), kNow).accepted());
  TEST_ASSERT_TRUE(r.receive(topic, payload("id0", kGran, kNow), twoGroups(), kNow).accepted());
  TEST_ASSERT_TRUE(r.receive(topic, payload("id31", kGran, kNow), twoGroups(), kNow).drop == DropReason::Duplicate);

  RecentIds ring(2);
  ring.add("a");
  ring.add("b");
  ring.add("c");
  TEST_ASSERT_FALSE(ring.contains("a"));
  TEST_ASSERT_TRUE(ring.contains("b") && ring.contains("c"));
  TEST_ASSERT_EQUAL_UINT32(2, ring.size());
}

static void test_membership_changes_take_effect_for_the_next_press() {
  PressReceiver r = receiver();
  Membership m = twoGroups();
  TEST_ASSERT_TRUE(r.receive(pressTopic(kG2, kGran), payload("a", kGran, kNow), m, kNow).accepted());
  m.groups.pop_back();  // removed from G2
  TEST_ASSERT_TRUE(r.receive(pressTopic(kG2, kGran), payload("b", kGran, kNow), m, kNow).drop == DropReason::NotMember);
}

// ---- 4.3 playback_queue ----

namespace {
PlaybackItem item(const std::string& id, uint32_t songMs) {
  PlaybackItem it;
  it.message.id = id;
  it.message.from = kGran;
  Note n;
  n.freqHz = 523;
  n.durMs = songMs;
  if (songMs > 0) it.melody.notes.push_back(n);
  return it;
}
}  // namespace

static void test_lights_last_at_least_ten_seconds() {
  TEST_ASSERT_EQUAL_UINT32(10000, PlaybackScheduler::lightsDurationMs(0));
  TEST_ASSERT_EQUAL_UINT32(10000, PlaybackScheduler::lightsDurationMs(4000));
  TEST_ASSERT_EQUAL_UINT32(20000, PlaybackScheduler::lightsDurationMs(20000));
  TEST_ASSERT_EQUAL_UINT32(30000, PlaybackScheduler::lightsDurationMs(30000));
  TEST_ASSERT_EQUAL_UINT32(30000, PlaybackScheduler::lightsDurationMs(45000));  // song is capped at 30 s
}

static void test_four_second_song_keeps_lights_until_ten_seconds_then_fades() {
  PlaybackScheduler s;
  TEST_ASSERT_TRUE(s.idle());
  TEST_ASSERT_TRUE(s.enqueue(item("a", 4000), 1000));
  TEST_ASSERT_FALSE(s.idle());
  TEST_ASSERT_TRUE(s.update(1000).started);
  TEST_ASSERT_TRUE(s.phase() == PlaybackScheduler::Phase::Playing);
  TEST_ASSERT_NOT_NULL(s.current());
  TEST_ASSERT_EQUAL_STRING("a", s.current()->message.id.c_str());
  TEST_ASSERT_FALSE(s.update(5000).any());  // the song has finished but the lights carry on
  TEST_ASSERT_FALSE(s.update(10999).any());
  PlaybackScheduler::Events ev = s.update(11000);  // 10 s after the start
  TEST_ASSERT_TRUE(ev.fadeStarted && !ev.ended);
  TEST_ASSERT_TRUE(s.phase() == PlaybackScheduler::Phase::Fading);
  TEST_ASSERT_FALSE(s.update(11999).ended);
  ev = s.update(12000);  // fade done
  TEST_ASSERT_TRUE(ev.ended);
  TEST_ASSERT_TRUE(s.phase() == PlaybackScheduler::Phase::Idle);
  TEST_ASSERT_NULL(s.current());
  TEST_ASSERT_TRUE(s.idle());
}

static void test_a_long_song_is_clipped_at_thirty_seconds() {
  PlaybackScheduler s;
  s.enqueue(item("long", 45000), 0);
  s.update(0);
  TEST_ASSERT_FALSE(s.update(29999).fadeStarted);
  TEST_ASSERT_TRUE(s.update(30000).fadeStarted);
  TEST_ASSERT_TRUE(s.update(31000).ended);
}

static void test_back_to_back_presses_play_in_order_with_a_one_second_pause() {
  PlaybackScheduler s;
  s.enqueue(item("first", 4000), 0);
  s.update(0);
  TEST_ASSERT_TRUE(s.enqueue(item("second", 4000), 500));   // arrives while the first plays
  TEST_ASSERT_TRUE(s.enqueue(item("third", 4000), 600));
  TEST_ASSERT_EQUAL_UINT32(2, s.waiting());
  TEST_ASSERT_TRUE(s.update(10000).fadeStarted);
  PlaybackScheduler::Events ev = s.update(11000);
  TEST_ASSERT_TRUE(ev.ended);
  TEST_ASSERT_TRUE_MESSAGE(s.phase() == PlaybackScheduler::Phase::Gap, "a pause, not an immediate start");
  TEST_ASSERT_NULL(s.current());
  TEST_ASSERT_FALSE(s.idle());
  TEST_ASSERT_FALSE(s.update(11999).started);
  ev = s.update(12000);
  TEST_ASSERT_TRUE(ev.started);
  TEST_ASSERT_EQUAL_STRING("second", s.current()->message.id.c_str());
  TEST_ASSERT_EQUAL_UINT32(1, s.waiting());
  // The second plays in full, then a pause, then the third.
  TEST_ASSERT_TRUE(s.update(22000).fadeStarted);
  TEST_ASSERT_TRUE(s.update(23000).ended);
  TEST_ASSERT_TRUE(s.update(24000).started);
  TEST_ASSERT_EQUAL_STRING("third", s.current()->message.id.c_str());
  TEST_ASSERT_TRUE(s.update(34000).fadeStarted);
  TEST_ASSERT_TRUE(s.update(35000).ended);
  TEST_ASSERT_TRUE(s.idle());
}

static void test_at_most_five_presses_wait() {
  PlaybackScheduler s;
  TEST_ASSERT_TRUE(s.enqueue(item("p0", 1000), 0));  // starts straight away
  s.update(0);
  for (int i = 1; i <= 5; ++i) TEST_ASSERT_TRUE(s.enqueue(item("p" + std::to_string(i), 1000), 10));
  TEST_ASSERT_EQUAL_UINT32(5, s.waiting());
  TEST_ASSERT_FALSE_MESSAGE(s.enqueue(item("p6", 1000), 20), "the sixth waiting press is discarded");
  TEST_ASSERT_FALSE(s.enqueue(item("p7", 1000), 30));
  TEST_ASSERT_EQUAL_UINT32(5, s.waiting());
  // Once one finishes there is room again.
  s.update(10000);
  s.update(11000);
  s.update(12000);  // second starts, 4 waiting
  TEST_ASSERT_EQUAL_UINT32(4, s.waiting());
  TEST_ASSERT_TRUE(s.enqueue(item("p8", 1000), 12100));
}

static void test_a_burst_before_the_first_update_is_not_over_counted() {
  PlaybackScheduler s;
  for (int i = 0; i < 6; ++i) TEST_ASSERT_TRUE(s.enqueue(item("b" + std::to_string(i), 1000), 0));  // 1 playing + 5 waiting
  TEST_ASSERT_FALSE(s.enqueue(item("b6", 1000), 0));
}

static void test_play_now_interrupts_but_keeps_the_queue() {
  PlaybackScheduler s;
  s.enqueue(item("a", 4000), 0);
  s.update(0);
  s.enqueue(item("b", 4000), 100);
  PlaybackItem preview = item("preview", 3000);
  preview.preview = true;
  s.playNow(preview, 2000);
  TEST_ASSERT_TRUE(s.update(2000).started);
  TEST_ASSERT_EQUAL_STRING("preview", s.current()->message.id.c_str());
  TEST_ASSERT_TRUE(s.current()->preview);
  TEST_ASSERT_EQUAL_UINT32(1, s.waiting());
  TEST_ASSERT_EQUAL_UINT32(0, s.elapsedInItem(2000));
  TEST_ASSERT_EQUAL_UINT32(5000, s.elapsedInItem(7000));
  s.update(12000);
  s.update(13000);
  TEST_ASSERT_TRUE(s.update(14000).started);
  TEST_ASSERT_EQUAL_STRING("b", s.current()->message.id.c_str());
}

static void test_clear_drops_everything() {
  PlaybackScheduler s;
  s.enqueue(item("a", 1000), 0);
  s.enqueue(item("b", 1000), 0);
  s.clear();
  TEST_ASSERT_TRUE(s.idle());
  TEST_ASSERT_FALSE(s.update(5).any());
}

static void test_a_very_late_update_catches_up_without_hanging() {
  PlaybackScheduler s;
  s.enqueue(item("a", 1000), 0);
  s.enqueue(item("b", 1000), 0);
  s.enqueue(item("c", 1000), 0);
  const PlaybackScheduler::Events ev = s.update(500000);
  TEST_ASSERT_TRUE(ev.started && ev.ended);
  // It must end in a consistent state and keep working.
  for (uint32_t t = 500000; t < 560000; t += 500) s.update(t);
  TEST_ASSERT_TRUE(s.idle());
}

static void test_playback_survives_millis_wraparound() {
  PlaybackScheduler s;
  const uint32_t start = 0xFFFFF000u;
  s.enqueue(item("w", 4000), start);
  s.update(start);
  TEST_ASSERT_FALSE(s.update(start + 5000).any());  // wrapped
  TEST_ASSERT_TRUE(s.update(start + 10000).fadeStarted);
  TEST_ASSERT_TRUE(s.update(start + 11000).ended);
}

void run_topics_tests() {
  RUN_TEST(test_topic_builders);
  RUN_TEST(test_press_topic_parsing);
  RUN_TEST(test_drop_reason_names);
  RUN_TEST(test_a_valid_press_is_accepted);
  RUN_TEST(test_duplicate_delivery_plays_once);
  RUN_TEST(test_same_press_through_two_shared_groups_plays_once);
  RUN_TEST(test_forged_sender_is_dropped);
  RUN_TEST(test_own_press_is_ignored);
  RUN_TEST(test_press_from_a_group_we_are_not_in_is_dropped);
  RUN_TEST(test_stale_presses_are_dropped_but_recent_and_unknown_time_are_played);
  RUN_TEST(test_malformed_payloads_and_topics_are_dropped);
  RUN_TEST(test_filters_apply_in_the_documented_order);
  RUN_TEST(test_message_id_ring_remembers_the_last_32);
  RUN_TEST(test_membership_changes_take_effect_for_the_next_press);
  RUN_TEST(test_lights_last_at_least_ten_seconds);
  RUN_TEST(test_four_second_song_keeps_lights_until_ten_seconds_then_fades);
  RUN_TEST(test_a_long_song_is_clipped_at_thirty_seconds);
  RUN_TEST(test_back_to_back_presses_play_in_order_with_a_one_second_pause);
  RUN_TEST(test_at_most_five_presses_wait);
  RUN_TEST(test_a_burst_before_the_first_update_is_not_over_counted);
  RUN_TEST(test_play_now_interrupts_but_keeps_the_queue);
  RUN_TEST(test_clear_drops_everything);
  RUN_TEST(test_a_very_late_update_catches_up_without_hanging);
  RUN_TEST(test_playback_survives_millis_wraparound);
}
