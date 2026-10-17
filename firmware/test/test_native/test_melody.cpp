#include <unity.h>

#include <string>

#include "melody_player.h"
#include "rtttl.h"
#include "song_library.h"

using namespace yule;

static Melody melodyFrom(const std::string& text) {
  RtttlResult r = parseRtttl(text);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, (text + " -> " + r.error.message).c_str());
  return r.melody;
}

// ---- melody_player ----

static void test_note_timing_at_known_tempo() {
  // 120 bpm: every quarter note is exactly 500 ms. c5 = 523 Hz, e5 = 659 Hz, g5 = 784 Hz.
  MelodyPlayer p;
  p.start(melodyFrom("T:d=4,o=5,b=120:c,e,g"), 1000);
  TEST_ASSERT_TRUE(p.playing());
  TEST_ASSERT_EQUAL_UINT32(1500, p.totalMs());
  TEST_ASSERT_EQUAL_UINT16(523, p.update(1000));
  TEST_ASSERT_EQUAL_UINT16(523, p.update(1100));
  TEST_ASSERT_EQUAL_UINT16(523, p.update(1400));
  TEST_ASSERT_EQUAL_UINT16(659, p.update(1500));
  TEST_ASSERT_EQUAL_UINT16(659, p.update(1700));
  TEST_ASSERT_EQUAL_UINT16(784, p.update(2000));
  TEST_ASSERT_TRUE(p.playing());
  TEST_ASSERT_EQUAL_UINT16(0, p.update(2500));
  TEST_ASSERT_FALSE(p.playing());
}

static void test_articulation_gap_separates_notes() {
  MelodyPlayer p;
  p.start(melodyFrom("T:d=4,o=5,b=120:c,c"), 0);
  const uint32_t gap = MelodyPlayer::articulationGapMs(500);
  TEST_ASSERT_TRUE(gap >= 5 && gap <= 30);
  TEST_ASSERT_EQUAL_UINT16(523, p.update(500 - gap - 1));
  TEST_ASSERT_EQUAL_UINT16(0, p.update(500 - gap));  // silent gap at the end of the note
  TEST_ASSERT_EQUAL_UINT16(0, p.update(499));
  TEST_ASSERT_EQUAL_UINT16(523, p.update(500));      // next note sounds again
  // The gap never swallows a whole short note.
  for (uint32_t d = 1; d < 400; ++d) TEST_ASSERT_TRUE(MelodyPlayer::articulationGapMs(d) <= d / 2);
}

static void test_rests_are_silent() {
  MelodyPlayer p;
  p.start(melodyFrom("T:d=4,o=5,b=120:c,p,c"), 0);
  TEST_ASSERT_EQUAL_UINT16(523, p.update(100));
  TEST_ASSERT_EQUAL_UINT16(0, p.update(600));
  TEST_ASSERT_EQUAL_UINT16(0, p.update(900));
  TEST_ASSERT_EQUAL_UINT16(523, p.update(1100));
}

static void test_fifty_second_tune_stops_at_thirty_seconds() {
  std::string tune = "Long:d=4,o=5,b=120:";
  for (int i = 0; i < 100; ++i) tune += "c,";  // 100 quarter notes of 500 ms = 50 s
  MelodyPlayer p;
  p.start(melodyFrom(tune), 0);
  TEST_ASSERT_EQUAL_UINT32(50000, p.totalMs());
  TEST_ASSERT_EQUAL_UINT32(30000, p.clippedMs());
  TEST_ASSERT_EQUAL_UINT16(523, p.update(29900));
  TEST_ASSERT_TRUE(p.playing());
  TEST_ASSERT_EQUAL_UINT16(0, p.update(30000));
  TEST_ASSERT_FALSE(p.playing());
  TEST_ASSERT_EQUAL_UINT16(0, p.update(30500));  // stays silent
}

static void test_empty_melody_finishes_immediately() {
  MelodyPlayer p;
  p.start(Melody(), 5);
  TEST_ASSERT_FALSE(p.playing());
  TEST_ASSERT_EQUAL_UINT32(0, p.totalMs());
  TEST_ASSERT_EQUAL_UINT32(0, p.clippedMs());
  TEST_ASSERT_EQUAL_UINT16(0, p.update(5));
  TEST_ASSERT_EQUAL_UINT16(0, p.update(100));
}

static void test_update_before_start_and_after_stop_is_silent() {
  MelodyPlayer p;
  TEST_ASSERT_EQUAL_UINT16(0, p.update(10));
  p.start(melodyFrom("T:d=4,o=5,b=120:c,e,g"), 0);
  TEST_ASSERT_EQUAL_UINT16(523, p.update(10));
  p.stop();
  TEST_ASSERT_FALSE(p.playing());
  TEST_ASSERT_EQUAL_UINT16(0, p.update(20));
}

static void test_restart_replaces_current_melody() {
  MelodyPlayer p;
  p.start(melodyFrom("T:d=4,o=5,b=120:c,c,c"), 0);
  p.update(700);
  p.start(melodyFrom("T:d=4,o=5,b=120:g,g"), 1000);
  TEST_ASSERT_EQUAL_UINT16(784, p.update(1000));
  TEST_ASSERT_EQUAL_UINT16(784, p.update(1100));
}

static void test_survives_millis_wraparound() {
  MelodyPlayer p;
  const uint32_t start = 0xFFFFFF00u;
  p.start(melodyFrom("T:d=4,o=5,b=120:c,e"), start);
  TEST_ASSERT_EQUAL_UINT16(523, p.update(start + 100));
  TEST_ASSERT_EQUAL_UINT16(523, p.update(start + 200));    // still before the wrap
  TEST_ASSERT_EQUAL_UINT16(523, p.update(0x00000040u));    // wrapped: 320 ms in
  TEST_ASSERT_EQUAL_UINT16(659, p.update(0x00000200u));    // 768 ms in: second note
  TEST_ASSERT_EQUAL_UINT16(0, p.update(0x000003E0u));      // 992 ms in: gap
}

static void test_late_update_skips_to_right_note() {
  MelodyPlayer p;
  p.start(melodyFrom("T:d=4,o=5,b=120:c,e,g,c6"), 0);
  TEST_ASSERT_EQUAL_UINT16(784, p.update(1100));  // jumped straight into the third note
  TEST_ASSERT_EQUAL_UINT16(1047, p.update(1600));
}

// ---- song_library ----

static void test_library_has_at_least_eight_carols() {
  TEST_ASSERT_TRUE(carolCount() >= 8);
}

static void test_every_library_tune_parses_and_carols_fit_the_limit() {
  for (size_t i = 0; i < carolCount(); ++i) {
    const SongInfo& s = carolAt(i);
    const RtttlResult r = parseRtttl(s.rtttl);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, (std::string(s.id) + ": " + r.error.message + " at " +
                                    std::to_string(r.error.position)).c_str());
    TEST_ASSERT_TRUE_MESSAGE(r.melody.totalMs() <= 30000, s.id);
    TEST_ASSERT_TRUE_MESSAGE(r.melody.totalMs() >= 4000, (std::string(s.id) + " is too short").c_str());
    TEST_ASSERT_TRUE_MESSAGE(std::string(s.rtttl).size() <= 512, s.id);
    TEST_ASSERT_TRUE(std::string(s.title).size() > 0);
  }
}

static void test_library_ids_are_unique_and_findable() {
  for (size_t i = 0; i < carolCount(); ++i) {
    const SongInfo* found = findCarol(carolAt(i).id);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_PTR(&carolAt(i), found);
    for (size_t j = i + 1; j < carolCount(); ++j) {
      TEST_ASSERT_TRUE(std::string(carolAt(i).id) != std::string(carolAt(j).id));
    }
  }
  TEST_ASSERT_NULL(findCarol("no-such-carol"));
  TEST_ASSERT_NULL(findCarol(""));
}

static void test_default_carol_is_first() {
  TEST_ASSERT_EQUAL_PTR(&carolAt(0), &defaultCarol());
  TEST_ASSERT_EQUAL_STRING("jingle-bells", defaultCarol().id);
}

static void test_feedback_tones_and_chime_parse_and_are_short() {
  const char* tones[] = {kToneSent, kToneSendFailed, kToneMutedOff, kDefaultChime};
  for (const char* t : tones) {
    const RtttlResult r = parseRtttl(t);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, t);
    TEST_ASSERT_TRUE_MESSAGE(r.melody.totalMs() > 0 && r.melody.totalMs() <= 2000, t);
  }
  // Distinct tones.
  TEST_ASSERT_TRUE(std::string(kToneSent) != kToneSendFailed);
  TEST_ASSERT_TRUE(std::string(kToneSent) != kToneMutedOff);
  TEST_ASSERT_TRUE(std::string(kToneSendFailed) != kDefaultChime);
  // "sent" rises; "send failed" is low.
  const Melody sent = melodyFrom(kToneSent);
  TEST_ASSERT_TRUE(sent.notes.back().freqHz > sent.notes.front().freqHz);
  const Melody failed = melodyFrom(kToneSendFailed);
  for (const Note& n : failed.notes) TEST_ASSERT_TRUE(n.freqHz < 500);
}

void run_melody_tests() {
  RUN_TEST(test_note_timing_at_known_tempo);
  RUN_TEST(test_articulation_gap_separates_notes);
  RUN_TEST(test_rests_are_silent);
  RUN_TEST(test_fifty_second_tune_stops_at_thirty_seconds);
  RUN_TEST(test_empty_melody_finishes_immediately);
  RUN_TEST(test_update_before_start_and_after_stop_is_silent);
  RUN_TEST(test_restart_replaces_current_melody);
  RUN_TEST(test_survives_millis_wraparound);
  RUN_TEST(test_late_update_skips_to_right_note);
  RUN_TEST(test_library_has_at_least_eight_carols);
  RUN_TEST(test_every_library_tune_parses_and_carols_fit_the_limit);
  RUN_TEST(test_library_ids_are_unique_and_findable);
  RUN_TEST(test_default_carol_is_first);
  RUN_TEST(test_feedback_tones_and_chime_parse_and_are_short);
}
