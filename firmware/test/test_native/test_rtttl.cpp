#include <unity.h>

#include <string>

#include "rgb.h"
#include "rtttl.h"

using namespace yule;

static RtttlResult parseOk(const std::string& text) {
  RtttlResult r = parseRtttl(text);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, (text + " -> " + r.error.message).c_str());
  return r;
}

static void test_standard_tune_jingle() {
  const RtttlResult r = parseOk("Jingle:d=8,o=5,b=112:e,e,4e,e,e,4e,e,g,c.,16d,2e");
  TEST_ASSERT_EQUAL_STRING("Jingle", r.melody.name.c_str());
  TEST_ASSERT_EQUAL_UINT32(11, r.melody.notes.size());
  const auto& n = r.melody.notes;
  // 112 bpm: a quarter note is 535.7 ms.
  TEST_ASSERT_EQUAL_UINT16(659, n[0].freqHz);   // e5
  TEST_ASSERT_EQUAL_UINT32(268, n[0].durMs);    // eighth
  TEST_ASSERT_EQUAL_UINT32(536, n[2].durMs);    // 4e: quarter
  TEST_ASSERT_EQUAL_UINT16(784, n[7].freqHz);   // g5
  TEST_ASSERT_EQUAL_UINT16(523, n[8].freqHz);   // c5
  TEST_ASSERT_EQUAL_UINT32(402, n[8].durMs);    // c.: dotted eighth
  TEST_ASSERT_EQUAL_UINT16(587, n[9].freqHz);   // d5
  TEST_ASSERT_EQUAL_UINT32(134, n[9].durMs);    // 16d: sixteenth
  TEST_ASSERT_EQUAL_UINT32(1071, n[10].durMs);  // 2e: half
  // Relative durations hold: a quarter is twice an eighth, a half is twice a quarter.
  TEST_ASSERT_UINT32_WITHIN(1, n[0].durMs * 2, n[2].durMs);
  TEST_ASSERT_UINT32_WITHIN(2, n[2].durMs * 2, n[10].durMs);
}

static void test_empty_defaults_use_standard_values() {
  const RtttlResult r = parseOk("Tune::c,e,g");
  TEST_ASSERT_EQUAL_UINT32(3, r.melody.notes.size());
  for (const Note& note : r.melody.notes) {
    TEST_ASSERT_EQUAL_UINT32(952, note.durMs);  // quarter note at 63 bpm
  }
  TEST_ASSERT_EQUAL_UINT16(1047, r.melody.notes[0].freqHz);  // c6
  TEST_ASSERT_EQUAL_UINT16(1319, r.melody.notes[1].freqHz);  // e6
  TEST_ASSERT_EQUAL_UINT16(1568, r.melody.notes[2].freqHz);  // g6
}

static void test_partial_defaults() {
  const RtttlResult r = parseOk("T:o=5:c");
  TEST_ASSERT_EQUAL_UINT16(523, r.melody.notes[0].freqHz);  // octave 5
  TEST_ASSERT_EQUAL_UINT32(952, r.melody.notes[0].durMs);   // d=4, b=63 still defaulted
  const RtttlResult r2 = parseOk("T:b=120:c");
  TEST_ASSERT_EQUAL_UINT32(500, r2.melody.notes[0].durMs);
  TEST_ASSERT_EQUAL_UINT16(1047, r2.melody.notes[0].freqHz);
}

static void test_invalid_note_reports_position() {
  const std::string text = "Test:d=4,o=5,b=120:c,h,e";
  const RtttlResult r = parseRtttl(text);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_UINT32(text.find('h') + 1, r.error.position);
  TEST_ASSERT_TRUE(r.error.message.size() > 0);
}

static void test_invalid_duration_reports_position() {
  const std::string text = "Test:d=4,o=5,b=120:c,3e,e";
  const RtttlResult r = parseRtttl(text);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_UINT32(text.find('3') + 1, r.error.position);
}

static void test_all_standard_durations_and_nothing_else() {
  const int good[] = {1, 2, 4, 8, 16, 32};
  for (int d : good) {
    const std::string text = "T:o=5,b=120:" + std::to_string(d) + "c";
    parseOk(text);
  }
  const int bad[] = {0, 3, 5, 6, 7, 9, 12, 24, 64};
  for (int d : bad) {
    const RtttlResult r = parseRtttl("T:o=5,b=120:" + std::to_string(d) + "c");
    TEST_ASSERT_FALSE_MESSAGE(r.ok, std::to_string(d).c_str());
  }
}

static void test_dot_before_or_after_octave() {
  const RtttlResult a = parseOk("T:d=4,o=5,b=120:c.6");
  const RtttlResult b = parseOk("T:d=4,o=5,b=120:c6.");
  const RtttlResult plain = parseOk("T:d=4,o=5,b=120:c6");
  TEST_ASSERT_EQUAL_UINT16(plain.melody.notes[0].freqHz, a.melody.notes[0].freqHz);
  TEST_ASSERT_EQUAL_UINT16(plain.melody.notes[0].freqHz, b.melody.notes[0].freqHz);
  TEST_ASSERT_EQUAL_UINT32(a.melody.notes[0].durMs, b.melody.notes[0].durMs);
  TEST_ASSERT_UINT32_WITHIN(1, plain.melody.notes[0].durMs * 3 / 2, a.melody.notes[0].durMs);
  // Sharp, dot and octave together.
  const RtttlResult s = parseOk("T:d=4,o=5,b=120:c#.6,8d#6.");
  TEST_ASSERT_EQUAL_UINT32(2, s.melody.notes.size());
}

static void test_whitespace_and_case_tolerance() {
  const RtttlResult ref = parseOk("Jingle:d=8,o=5,b=112:e,e,4e,g,c.,2e");
  const RtttlResult messy = parseOk("  Jingle : D=8 , O=5 ,\tB=112 :\n E , e,\r\n 4E, G, C. ,2e  ");
  TEST_ASSERT_EQUAL_UINT32(ref.melody.notes.size(), messy.melody.notes.size());
  for (size_t i = 0; i < ref.melody.notes.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT16(ref.melody.notes[i].freqHz, messy.melody.notes[i].freqHz);
    TEST_ASSERT_EQUAL_UINT32(ref.melody.notes[i].durMs, messy.melody.notes[i].durMs);
  }
  TEST_ASSERT_EQUAL_STRING("Jingle", messy.melody.name.c_str());
  // A space between a duration and its note is fine; between a note and the next note is not.
  parseOk("T:d=4,o=5,b=120:8 c");
  TEST_ASSERT_FALSE(parseRtttl("T:d=4,o=5,b=120:c e").ok);
}

static void test_pauses() {
  const RtttlResult r = parseOk("T:d=4,o=5,b=120:p,8p,c,2p.");
  TEST_ASSERT_EQUAL_UINT32(4, r.melody.notes.size());
  TEST_ASSERT_EQUAL_UINT16(0, r.melody.notes[0].freqHz);
  TEST_ASSERT_EQUAL_UINT16(0, r.melody.notes[1].freqHz);
  TEST_ASSERT_TRUE(r.melody.notes[2].freqHz > 0);
  TEST_ASSERT_EQUAL_UINT16(0, r.melody.notes[3].freqHz);
  TEST_ASSERT_EQUAL_UINT32(500, r.melody.notes[0].durMs);
  TEST_ASSERT_EQUAL_UINT32(250, r.melody.notes[1].durMs);
  TEST_ASSERT_EQUAL_UINT32(1500, r.melody.notes[3].durMs);  // dotted half
  TEST_ASSERT_FALSE_MESSAGE(parseRtttl("T:d=4:p#").ok, "a pause cannot be sharp");
}

static void test_sharps() {
  const RtttlResult r = parseOk("T:d=4,o=5,b=120:c#,f#6,a#4,e#,b#");
  const auto& n = r.melody.notes;
  TEST_ASSERT_EQUAL_UINT16(554, n[0].freqHz);   // c#5
  TEST_ASSERT_EQUAL_UINT16(1480, n[1].freqHz);  // f#6
  TEST_ASSERT_EQUAL_UINT16(466, n[2].freqHz);   // a#4
  TEST_ASSERT_EQUAL_UINT16(698, n[3].freqHz);   // e#5 is f5
  TEST_ASSERT_EQUAL_UINT16(1047, n[4].freqHz);  // b#5 is c6
}

static void test_octave_range() {
  TEST_ASSERT_EQUAL_UINT16(131, parseOk("T:d=4:c3").melody.notes[0].freqHz);
  TEST_ASSERT_EQUAL_UINT16(262, parseOk("T:d=4:c4").melody.notes[0].freqHz);
  TEST_ASSERT_EQUAL_UINT16(2093, parseOk("T:d=4:c7").melody.notes[0].freqHz);
  TEST_ASSERT_EQUAL_UINT16(4186, parseOk("T:d=4:c8").melody.notes[0].freqHz);
  TEST_ASSERT_FALSE(parseRtttl("T:d=4:c2").ok);
  TEST_ASSERT_FALSE(parseRtttl("T:d=4:c9").ok);
  TEST_ASSERT_FALSE(parseRtttl("T:o=2:c").ok);
  TEST_ASSERT_FALSE(parseRtttl("T:o=9:c").ok);
}

static void test_note_frequency_reference_values() {
  TEST_ASSERT_EQUAL_UINT16(440, noteFrequencyHz(4, 9));  // A4
  TEST_ASSERT_EQUAL_UINT16(880, noteFrequencyHz(5, 9));  // A5
  TEST_ASSERT_EQUAL_UINT16(262, noteFrequencyHz(4, 0));  // middle C
}

static void test_structure_errors() {
  RtttlResult r = parseRtttl("abc");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_UINT32(4, r.error.position);
  r = parseRtttl("abc:d=4");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_UINT32(8, r.error.position);
  r = parseRtttl("");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_UINT32(1, r.error.position);
  r = parseRtttl("T:d=4:");
  TEST_ASSERT_FALSE_MESSAGE(r.ok, "a tune with no notes is not playable");
  r = parseRtttl("T:d=4: , ,");
  TEST_ASSERT_FALSE(r.ok);
}

static void test_defaults_errors() {
  RtttlResult r = parseRtttl("T:d=3:c");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_UINT32(5, r.error.position);
  TEST_ASSERT_FALSE(parseRtttl("T:b=5:c").ok);
  TEST_ASSERT_FALSE(parseRtttl("T:b=901:c").ok);
  TEST_ASSERT_FALSE(parseRtttl("T:b=0:c").ok);  // would divide by zero
  TEST_ASSERT_FALSE(parseRtttl("T:x=4:c").ok);
  TEST_ASSERT_FALSE(parseRtttl("T:d:c").ok);
  TEST_ASSERT_FALSE(parseRtttl("T:d=:c").ok);
  TEST_ASSERT_FALSE(parseRtttl("T:d=99999999999:c").ok);
  parseOk("T:b=10:c");
  parseOk("T:b=900:c");
}

static void test_trailing_commas_and_empty_items_tolerated() {
  TEST_ASSERT_EQUAL_UINT32(2, parseOk("T:d=4,o=5,b=120:c,e,").melody.notes.size());
  TEST_ASSERT_EQUAL_UINT32(2, parseOk("T:d=4,o=5,b=120:c,,e").melody.notes.size());
  TEST_ASSERT_EQUAL_UINT32(1, parseOk("T:,d=4,,o=5,:c").melody.notes.size());
}

static void test_stray_characters_rejected() {
  TEST_ASSERT_FALSE(parseRtttl("T:d=4:c..").ok);  // two dots
  TEST_ASSERT_FALSE(parseRtttl("T:d=4:c66").ok);  // two octave digits
  TEST_ASSERT_FALSE(parseRtttl("T:d=4:c#d").ok);
  TEST_ASSERT_FALSE(parseRtttl("T:d=4:8").ok);  // duration without a note
  TEST_ASSERT_FALSE(parseRtttl("T:d=4:c;e").ok);
  TEST_ASSERT_FALSE(parseRtttl("T:d=4:c:e").ok);
}

static void test_random_garbage_never_crashes() {
  Prng prng(2024);
  const char alphabet[] = "abcdefgpABCH#.,:=0123456789 \t\n;-";
  for (int i = 0; i < 3000; ++i) {
    const size_t len = prng.below(80);
    std::string s;
    for (size_t k = 0; k < len; ++k) s.push_back(alphabet[prng.below(sizeof(alphabet) - 1)]);
    const RtttlResult r = parseRtttl(s);
    if (!r.ok) TEST_ASSERT_TRUE(r.error.position >= 1 && r.error.position <= s.size() + 1);
  }
  // Raw bytes, including NUL and high bit.
  for (int i = 0; i < 500; ++i) {
    std::string s(prng.below(40), '\0');
    for (char& c : s) c = static_cast<char>(prng.below(256));
    parseRtttl(s);
  }
  // A very long tune parses without trouble.
  std::string big = "Big:d=16,o=5,b=200:";
  for (int i = 0; i < 5000; ++i) big += "c,";
  TEST_ASSERT_TRUE(parseRtttl(big).ok);
}

static void test_total_duration_adds_up() {
  const RtttlResult r = parseOk("T:d=4,o=5,b=120:c,e,g,2c6");
  TEST_ASSERT_EQUAL_UINT32(500 + 500 + 500 + 1000, r.melody.totalMs());
}

void run_rtttl_tests() {
  RUN_TEST(test_standard_tune_jingle);
  RUN_TEST(test_empty_defaults_use_standard_values);
  RUN_TEST(test_partial_defaults);
  RUN_TEST(test_invalid_note_reports_position);
  RUN_TEST(test_invalid_duration_reports_position);
  RUN_TEST(test_all_standard_durations_and_nothing_else);
  RUN_TEST(test_dot_before_or_after_octave);
  RUN_TEST(test_whitespace_and_case_tolerance);
  RUN_TEST(test_pauses);
  RUN_TEST(test_sharps);
  RUN_TEST(test_octave_range);
  RUN_TEST(test_note_frequency_reference_values);
  RUN_TEST(test_structure_errors);
  RUN_TEST(test_defaults_errors);
  RUN_TEST(test_trailing_commas_and_empty_items_tolerated);
  RUN_TEST(test_stray_characters_rejected);
  RUN_TEST(test_random_garbage_never_crashes);
  RUN_TEST(test_total_duration_adds_up);
}
