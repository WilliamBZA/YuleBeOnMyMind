// Tests for press_message (4.1), status_payload (4.4), membership_config (4.5), send_tracker (4.6)
// and device_id (6.1).
#include <unity.h>

#include <string>
#include <vector>

#include "device_id.h"
#include "membership_config.h"
#include "press_message.h"
#include "send_tracker.h"
#include "song_library.h"
#include "status_payload.h"

using namespace yule;

namespace {

const char* kJingle = "Jingle:d=8,o=5,b=112:e,e,4e,e,e,4e,e,g,c.,16d,2e";

PressMessage sample() {
  PressMessage m;
  m.id = "9f3c2a17b04e6d58";
  m.from = "d-3c71bf4a2b10";
  m.name = "Gran";
  m.ts = 1797000000;
  m.song = kJingle;
  m.pattern.effect = Effect::Twinkle;
  m.pattern.colorCount = 2;
  m.pattern.colors[0] = Rgb(0xC8, 0x10, 0x2E);
  m.pattern.colors[1] = Rgb(0x00, 0x84, 0x3D);
  m.pattern.speed = 2;
  return m;
}

PressDecodeResult decode(const std::string& s) { return decodePress(s); }

std::string press(const std::string& extra, const std::string& from = "\"d-3c71bf4a2b10\"",
                  const std::string& id = "\"abc123\"") {
  std::string s = "{\"v\":1,\"id\":" + id + ",\"from\":" + from;
  if (!extra.empty()) s += "," + extra;
  return s + "}";
}

}  // namespace

// ---- 4.1 press_message ----

static void test_press_encodes_to_the_documented_wire_format() {
  TEST_ASSERT_EQUAL_STRING(
      "{\"v\":1,\"id\":\"9f3c2a17b04e6d58\",\"from\":\"d-3c71bf4a2b10\",\"name\":\"Gran\",\"ts\":1797000000,"
      "\"song\":\"Jingle:d=8,o=5,b=112:e,e,4e,e,e,4e,e,g,c.,16d,2e\","
      "\"pattern\":{\"effect\":\"twinkle\",\"colors\":[\"#C8102E\",\"#00843D\"],\"speed\":2}}",
      encodePress(sample()).c_str());
}

static void test_press_without_a_synchronised_clock_has_no_timestamp() {
  PressMessage m = sample();
  m.ts = 0;
  TEST_ASSERT_TRUE(encodePress(m).find("\"ts\"") == std::string::npos);
}

static void test_press_round_trips() {
  const PressMessage in = sample();
  const PressDecodeResult out = decode(encodePress(in));
  TEST_ASSERT_TRUE(out.status == PressDecodeStatus::Ok);
  TEST_ASSERT_EQUAL_INT(1, out.message.version);
  TEST_ASSERT_EQUAL_STRING(in.id.c_str(), out.message.id.c_str());
  TEST_ASSERT_EQUAL_STRING(in.from.c_str(), out.message.from.c_str());
  TEST_ASSERT_EQUAL_STRING(in.name.c_str(), out.message.name.c_str());
  TEST_ASSERT_EQUAL_INT64(in.ts, out.message.ts);
  TEST_ASSERT_EQUAL_STRING(in.song.c_str(), out.message.song.c_str());
  TEST_ASSERT_TRUE(in.pattern == out.message.pattern);
}

static void test_press_from_newer_firmware_is_understood() {
  const std::string payload =
      "{\"v\":2,\"id\":\"newer01\",\"from\":\"d-3c71bf4a2b10\",\"name\":\"Gran\",\"ts\":1797000000,"
      "\"song\":\"Jingle:d=8,o=5,b=112:e,e,4e\",\"future\":{\"a\":[1,2,3]},"
      "\"pattern\":{\"effect\":\"plasma\",\"colors\":[\"#FF0000\",\"#0000FF\"],\"speed\":3,\"intensity\":9}}";
  const PressDecodeResult r = decode(payload);
  TEST_ASSERT_TRUE(r.status == PressDecodeStatus::Ok);
  TEST_ASSERT_EQUAL_INT(2, r.message.version);
  TEST_ASSERT_TRUE_MESSAGE(r.message.pattern.effect == Effect::Breathe, "unknown effect shows as breathe");
  TEST_ASSERT_EQUAL_UINT8(2, r.message.pattern.colorCount);
  TEST_ASSERT_TRUE(r.message.pattern.colors[0] == Rgb(255, 0, 0));
  TEST_ASSERT_TRUE(r.message.pattern.colors[1] == Rgb(0, 0, 255));
  TEST_ASSERT_EQUAL_UINT8(3, r.message.pattern.speed);
  TEST_ASSERT_EQUAL_STRING("Jingle:d=8,o=5,b=112:e,e,4e", r.message.song.c_str());
}

static void test_garbage_and_truncated_payloads_are_rejected_without_crashing() {
  const char* garbage[] = {"", "garbage", "{", "}", "[]", "123", "null", "\"text\"", "{\"from\":", "{{{{", "\xff\xfe\x00\x01",
                           "{\"from\":\"d-3c71bf4a2b10\",\"id\":", "[{\"from\":\"d-3c71bf4a2b10\"}]"};
  for (const char* g : garbage) {
    TEST_ASSERT_TRUE_MESSAGE(decode(g).status != PressDecodeStatus::Ok, g);
  }
  // Every strict prefix of a valid message is invalid JSON, so none may be accepted.
  const std::string whole = encodePress(sample());
  for (size_t len = 0; len < whole.size(); ++len) {
    TEST_ASSERT_TRUE(decode(whole.substr(0, len)).status != PressDecodeStatus::Ok);
  }
  TEST_ASSERT_TRUE(decode(whole).status == PressDecodeStatus::Ok);
  // Hostile shapes: very deep nesting and an oversized payload.
  std::string deep(5000, '[');
  TEST_ASSERT_TRUE(decode(deep).status != PressDecodeStatus::Ok);
  std::string big = press("\"name\":\"" + std::string(3000, 'x') + "\"");
  TEST_ASSERT_TRUE(decode(big).status == PressDecodeStatus::Malformed);
}

static void test_a_missing_or_invalid_sender_is_rejected() {
  TEST_ASSERT_TRUE(decode("{\"v\":1,\"id\":\"abc\",\"song\":\"x\"}").status == PressDecodeStatus::MissingFrom);
  TEST_ASSERT_TRUE(decode(press("", "\"somebody\"")).status == PressDecodeStatus::MissingFrom);
  TEST_ASSERT_TRUE(decode(press("", "\"D-3C71BF4A2B10\"")).status == PressDecodeStatus::MissingFrom);
  TEST_ASSERT_TRUE(decode(press("", "\"d-3c71bf4a2b1\"")).status == PressDecodeStatus::MissingFrom);
  TEST_ASSERT_TRUE(decode(press("", "123")).status == PressDecodeStatus::MissingFrom);
  TEST_ASSERT_TRUE(decode(press("", "null")).status == PressDecodeStatus::MissingFrom);
}

static void test_a_missing_message_id_is_rejected() {
  TEST_ASSERT_TRUE(decode("{\"v\":1,\"from\":\"d-3c71bf4a2b10\"}").status == PressDecodeStatus::MissingId);
  TEST_ASSERT_TRUE(decode(press("", "\"d-3c71bf4a2b10\"", "\"\"")).status == PressDecodeStatus::MissingId);
  TEST_ASSERT_TRUE(decode(press("", "\"d-3c71bf4a2b10\"", "\"has space\"")).status == PressDecodeStatus::MissingId);
  TEST_ASSERT_TRUE(decode(press("", "\"d-3c71bf4a2b10\"", "\"" + std::string(33, 'a') + "\"")).status == PressDecodeStatus::MissingId);
  TEST_ASSERT_TRUE(decode(press("", "\"d-3c71bf4a2b10\"", "7")).status == PressDecodeStatus::MissingId);
}

static void test_missing_pattern_and_song_get_defaults() {
  const PressDecodeResult r = decode(press(""));
  TEST_ASSERT_TRUE(r.status == PressDecodeStatus::Ok);
  TEST_ASSERT_TRUE(r.message.pattern == defaultPattern());
  TEST_ASSERT_EQUAL_STRING(kDefaultChime, r.message.song.c_str());
  TEST_ASSERT_EQUAL_INT64(0, r.message.ts);
  TEST_ASSERT_EQUAL_STRING("", r.message.name.c_str());
}

static void test_invalid_colors_become_warm_white() {
  auto colorsOf = [](const std::string& colors) {
    return decode(press("\"pattern\":{\"effect\":\"solid\",\"colors\":" + colors + "}"));
  };
  PressDecodeResult r = colorsOf("[]");
  TEST_ASSERT_EQUAL_UINT8(1, r.message.pattern.colorCount);
  TEST_ASSERT_TRUE(r.message.pattern.colors[0] == kWarmWhite);
  r = colorsOf("\"red\"");
  TEST_ASSERT_TRUE(r.message.pattern.colors[0] == kWarmWhite);
  r = colorsOf("[\"#GG0000\",\"#00FF00\"]");
  TEST_ASSERT_EQUAL_UINT8(2, r.message.pattern.colorCount);
  TEST_ASSERT_TRUE(r.message.pattern.colors[0] == kWarmWhite);
  TEST_ASSERT_TRUE(r.message.pattern.colors[1] == Rgb(0, 255, 0));
  r = colorsOf("[123,null,\"#0000ff\"]");
  TEST_ASSERT_EQUAL_UINT8(3, r.message.pattern.colorCount);
  TEST_ASSERT_TRUE(r.message.pattern.colors[0] == kWarmWhite);
  TEST_ASSERT_TRUE(r.message.pattern.colors[1] == kWarmWhite);
  TEST_ASSERT_TRUE(r.message.pattern.colors[2] == Rgb(0, 0, 255));
  r = colorsOf("[\"#111111\",\"#222222\",\"#333333\",\"#444444\",\"#555555\"]");
  TEST_ASSERT_EQUAL_UINT8(3, r.message.pattern.colorCount);  // at most three
}

static void test_invalid_speed_defaults_to_medium() {
  for (const char* s : {"0", "4", "-1", "\"fast\"", "null", "2.5e9"}) {
    const PressDecodeResult r = decode(press(std::string("\"pattern\":{\"effect\":\"solid\",\"speed\":") + s + "}"));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, r.message.pattern.speed, s);
  }
  const PressDecodeResult fast = decode(press("\"pattern\":{\"speed\":3}"));
  TEST_ASSERT_EQUAL_UINT8(3, fast.message.pattern.speed);
}

static void test_invalid_or_overlong_song_becomes_the_default_chime() {
  auto songOf = [](const std::string& song) { return decode(press("\"song\":" + song)).message.song; };
  TEST_ASSERT_EQUAL_STRING(kDefaultChime, songOf("\"not a tune\"").c_str());
  TEST_ASSERT_EQUAL_STRING(kDefaultChime, songOf("\"\"").c_str());
  TEST_ASSERT_EQUAL_STRING(kDefaultChime, songOf("42").c_str());
  TEST_ASSERT_EQUAL_STRING(kDefaultChime, songOf("\"T:d=3:c\"").c_str());
  std::string longTune = "T:d=4,o=5,b=120:";
  while (longTune.size() <= 512) longTune += "c,";
  TEST_ASSERT_EQUAL_STRING(kDefaultChime, songOf("\"" + longTune + "\"").c_str());
}

static void test_exactly_512_characters_of_song_is_accepted() {
  std::string tune = "T:d=4,o=5,b=120:c";  // 17 characters
  for (int i = 0; i < 246; ++i) tune += ",c";  // + 492
  tune += ",8c";                               // + 3 = 512
  TEST_ASSERT_EQUAL_UINT32(512, tune.size());
  const PressDecodeResult r = decode(press("\"song\":\"" + tune + "\""));
  TEST_ASSERT_EQUAL_STRING(tune.c_str(), r.message.song.c_str());
}

static void test_timestamp_handling() {
  TEST_ASSERT_EQUAL_INT64(1797000000, decode(press("\"ts\":1797000000")).message.ts);
  TEST_ASSERT_EQUAL_INT64(0, decode(press("\"ts\":-5")).message.ts);
  TEST_ASSERT_EQUAL_INT64(0, decode(press("\"ts\":\"yesterday\"")).message.ts);
  TEST_ASSERT_EQUAL_INT64(0, decode(press("\"ts\":0")).message.ts);
}

static void test_long_names_are_trimmed_and_capped() {
  const PressDecodeResult r = decode(press("\"name\":\"  Gran  \""));
  TEST_ASSERT_EQUAL_STRING("Gran", r.message.name.c_str());
  const PressDecodeResult l = decode(press("\"name\":\"" + std::string(200, 'n') + "\""));
  TEST_ASSERT_TRUE(l.message.name.size() <= 64);
}

// ---- 4.4 status payloads ----

static void test_status_payloads_are_exactly_as_documented() {
  StatusInfo info;
  info.firmwareVersion = "0.3.0";
  info.board = "esp32dev";
  info.displayName = "Gran";
  info.otaResult = "ok";
  TEST_ASSERT_EQUAL_STRING("{\"v\":1,\"state\":\"online\",\"fw\":\"0.3.0\",\"board\":\"esp32dev\",\"name\":\"Gran\",\"ota\":\"ok\"}",
                           encodeStatusOnline(info).c_str());
  TEST_ASSERT_EQUAL_STRING("{\"v\":1,\"state\":\"offline\"}", encodeStatusOffline().c_str());
  info.otaResult = "rolled-back:0.3.0";
  info.displayName = "Gran \"G\"";
  TEST_ASSERT_TRUE(encodeStatusOnline(info).find("\"ota\":\"rolled-back:0.3.0\"") != std::string::npos);
  TEST_ASSERT_TRUE(encodeStatusOnline(info).find("Gran \\\"G\\\"") != std::string::npos);  // escaped
  TEST_ASSERT_EQUAL_STRING("none", StatusInfo().otaResult.c_str());
}

// ---- 4.5 membership_config ----

static const char* kTwoGroups =
    "{\"v\":1,\"groups\":[{\"id\":\"g-7f3k2abc\",\"name\":\"Brander Grandparents\"},"
    "{\"id\":\"g-q7m4x2td\",\"name\":\"Smith Grandparents\"}],\"channel\":\"stable\"}";

static void test_config_decodes() {
  const ConfigResult r = decodeDeviceConfig(kTwoGroups);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error.c_str());
  TEST_ASSERT_EQUAL_UINT32(2, r.membership.groups.size());
  TEST_ASSERT_EQUAL_STRING("g-7f3k2abc", r.membership.groups[0].id.c_str());
  TEST_ASSERT_EQUAL_STRING("Smith Grandparents", r.membership.groups[1].name.c_str());
  TEST_ASSERT_EQUAL_STRING("stable", r.membership.channel.c_str());
  TEST_ASSERT_TRUE(r.membership.hasGroup("g-q7m4x2td"));
  TEST_ASSERT_FALSE(r.membership.hasGroup("g-zzzzzzzz"));
  TEST_ASSERT_EQUAL_UINT32(2, r.membership.groupNames().size());
}

static void test_config_round_trips_through_encode() {
  const ConfigResult r = decodeDeviceConfig(kTwoGroups);
  const ConfigResult again = decodeDeviceConfig(encodeMembership(r.membership));
  TEST_ASSERT_TRUE(again.ok);
  TEST_ASSERT_TRUE(r.membership == again.membership);
}

static void test_empty_group_list_is_valid() {
  const ConfigResult r = decodeDeviceConfig("{\"v\":1,\"groups\":[],\"channel\":\"beta\"}");
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_UINT32(0, r.membership.groups.size());
  TEST_ASSERT_EQUAL_STRING("beta", r.membership.channel.c_str());
}

static void test_config_ignores_unknown_fields_and_defaults_the_channel() {
  const ConfigResult r = decodeDeviceConfig(
      "{\"v\":3,\"groups\":[{\"id\":\"g-aaaaaaaa\",\"name\":\"A\",\"color\":\"red\"}],\"extra\":{\"x\":1}}");
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error.c_str());
  TEST_ASSERT_EQUAL_STRING("stable", r.membership.channel.c_str());
}

static void test_invalid_config_is_rejected_whole() {
  const char* bad[] = {
      "", "not json", "[]", "{}", "{\"v\":1}", "{\"v\":1,\"groups\":{}}", "{\"v\":0,\"groups\":[]}", "{\"v\":\"1\",\"groups\":[]}",
      // bad group ids
      "{\"groups\":[{\"id\":\"g-short\",\"name\":\"A\"}]}", "{\"groups\":[{\"id\":\"G-ABCDEFGH\",\"name\":\"A\"}]}",
      "{\"groups\":[{\"id\":\"x-aaaaaaaa\",\"name\":\"A\"}]}", "{\"groups\":[{\"id\":\"g-aaaaaaa!\",\"name\":\"A\"}]}",
      "{\"groups\":[{\"id\":\"g-aaaaaaaaa\",\"name\":\"A\"}]}", "{\"groups\":[{\"name\":\"A\"}]}",
      // bad names
      "{\"groups\":[{\"id\":\"g-aaaaaaaa\",\"name\":\"\"}]}", "{\"groups\":[{\"id\":\"g-aaaaaaaa\"}]}",
      "{\"groups\":[{\"id\":\"g-aaaaaaaa\",\"name\":5}]}", "{\"groups\":[\"g-aaaaaaaa\"]}",
      // bad channel
      "{\"groups\":[],\"channel\":\"nightly\"}", "{\"groups\":[],\"channel\":5}",
      // one bad entry spoils the lot
      "{\"groups\":[{\"id\":\"g-aaaaaaaa\",\"name\":\"Fine\"},{\"id\":\"bad\",\"name\":\"Bad\"}]}"};
  for (const char* b : bad) TEST_ASSERT_FALSE_MESSAGE(decodeDeviceConfig(b).ok, b);
}

static void test_group_name_length_limits() {
  auto withName = [](const std::string& name) {
    return decodeDeviceConfig("{\"groups\":[{\"id\":\"g-aaaaaaaa\",\"name\":\"" + name + "\"}]}");
  };
  TEST_ASSERT_TRUE(withName("A").ok);
  TEST_ASSERT_TRUE(withName(std::string(40, 'n')).ok);
  TEST_ASSERT_FALSE(withName(std::string(41, 'n')).ok);
  // Characters, not bytes: 40 two-byte characters is fine.
  std::string accented;
  for (int i = 0; i < 40; ++i) accented += "\xC3\xA9";
  TEST_ASSERT_TRUE(withName(accented).ok);
  TEST_ASSERT_FALSE(withName(accented + "\xC3\xA9").ok);
}

static void test_duplicate_groups_collapse_and_too_many_are_rejected() {
  const ConfigResult dup = decodeDeviceConfig(
      "{\"groups\":[{\"id\":\"g-aaaaaaaa\",\"name\":\"First\"},{\"id\":\"g-aaaaaaaa\",\"name\":\"Again\"}]}");
  TEST_ASSERT_TRUE(dup.ok);
  TEST_ASSERT_EQUAL_UINT32(1, dup.membership.groups.size());
  TEST_ASSERT_EQUAL_STRING("First", dup.membership.groups[0].name.c_str());

  std::string many = "{\"groups\":[";
  for (int i = 0; i < 33; ++i) {
    if (i) many += ",";
    std::string id = "g-" + std::string(6, 'a') + char('a' + i / 26) + char('a' + i % 26);
    many += "{\"id\":\"" + id + "\",\"name\":\"G\"}";
  }
  many += "]}";
  TEST_ASSERT_FALSE(decodeDeviceConfig(many).ok);
}

static Membership membershipOf(std::vector<std::string> ids, const std::string& channel = "stable") {
  Membership m;
  for (const std::string& id : ids) m.groups.push_back({id, "n"});
  m.channel = channel;
  return m;
}

static void test_diff_adding_a_group() {
  const MembershipDiff d = diffMembership(membershipOf({"g-aaaaaaaa"}), membershipOf({"g-aaaaaaaa", "g-bbbbbbbb"}));
  TEST_ASSERT_EQUAL_UINT32(1, d.subscribe.size());
  TEST_ASSERT_EQUAL_STRING("g-bbbbbbbb", d.subscribe[0].c_str());
  TEST_ASSERT_EQUAL_UINT32(0, d.unsubscribe.size());
  TEST_ASSERT_FALSE(d.channelChanged);
  TEST_ASSERT_FALSE(d.empty());
}

static void test_diff_removing_a_group() {
  const MembershipDiff d = diffMembership(membershipOf({"g-aaaaaaaa", "g-bbbbbbbb"}), membershipOf({"g-bbbbbbbb"}));
  TEST_ASSERT_EQUAL_UINT32(0, d.subscribe.size());
  TEST_ASSERT_EQUAL_UINT32(1, d.unsubscribe.size());
  TEST_ASSERT_EQUAL_STRING("g-aaaaaaaa", d.unsubscribe[0].c_str());
}

static void test_diff_channel_change_and_no_change_and_empty() {
  const MembershipDiff c = diffMembership(membershipOf({"g-aaaaaaaa"}, "stable"), membershipOf({"g-aaaaaaaa"}, "beta"));
  TEST_ASSERT_TRUE(c.channelChanged);
  TEST_ASSERT_EQUAL_UINT32(0, c.subscribe.size() + c.unsubscribe.size());
  TEST_ASSERT_TRUE(diffMembership(membershipOf({"g-aaaaaaaa"}), membershipOf({"g-aaaaaaaa"})).empty());
  // Renaming alone needs no subscription change.
  Membership renamed = membershipOf({"g-aaaaaaaa"});
  renamed.groups[0].name = "New name";
  TEST_ASSERT_TRUE(diffMembership(membershipOf({"g-aaaaaaaa"}), renamed).empty());
  // To an empty list: unsubscribe from everything.
  const MembershipDiff e = diffMembership(membershipOf({"g-aaaaaaaa", "g-bbbbbbbb"}), membershipOf({}));
  TEST_ASSERT_EQUAL_UINT32(2, e.unsubscribe.size());
  // From nothing (first boot): subscribe to everything.
  TEST_ASSERT_EQUAL_UINT32(2, diffMembership(membershipOf({}), membershipOf({"g-aaaaaaaa", "g-bbbbbbbb"})).subscribe.size());
}

static void test_previous_membership_is_kept_when_a_new_config_is_invalid() {
  // The caller applies a config only when decode succeeds; this documents that contract.
  Membership current = decodeDeviceConfig(kTwoGroups).membership;
  const ConfigResult bad = decodeDeviceConfig("{\"groups\":[{\"id\":\"nope\",\"name\":\"X\"}]}");
  if (bad.ok) current = bad.membership;
  TEST_ASSERT_EQUAL_UINT32(2, current.groups.size());
}

static void test_group_and_channel_validators() {
  TEST_ASSERT_TRUE(isValidGroupId("g-7f3k2abc"));
  TEST_ASSERT_TRUE(isValidGroupId("g-00000000"));
  TEST_ASSERT_FALSE(isValidGroupId("g-7f3k2ab"));
  TEST_ASSERT_FALSE(isValidGroupId("g-7F3K2ABC"));
  TEST_ASSERT_FALSE(isValidGroupId(""));
  TEST_ASSERT_TRUE(isValidChannel("stable"));
  TEST_ASSERT_TRUE(isValidChannel("beta"));
  TEST_ASSERT_FALSE(isValidChannel("Beta"));
  TEST_ASSERT_FALSE(isValidChannel(""));
}

// ---- 4.6 send_tracker ----

static void test_all_copies_acknowledged_is_success() {
  SendTracker t;
  t.begin(1000, {11, 12});
  TEST_ASSERT_TRUE(t.pending());
  t.onAck(11, 1100);
  TEST_ASSERT_TRUE(t.update(1200) == SendTracker::State::Pending);
  TEST_ASSERT_EQUAL_UINT32(1, t.outstanding());
  t.onAck(12, 1300);
  TEST_ASSERT_TRUE(t.state() == SendTracker::State::Succeeded);
  bool ok = false;
  TEST_ASSERT_TRUE(t.takeResult(ok));
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(t.state() == SendTracker::State::Idle);
  TEST_ASSERT_FALSE(t.takeResult(ok));  // reported once
}

static void test_one_missing_acknowledgement_fails_after_five_seconds() {
  SendTracker t;
  t.begin(1000, {1, 2, 3});
  t.onAck(1, 1010);
  t.onAck(3, 1020);
  TEST_ASSERT_TRUE(t.update(5999) == SendTracker::State::Pending);
  TEST_ASSERT_TRUE(t.update(6000) == SendTracker::State::Pending);  // 5000 ms exactly is still within 5 s
  TEST_ASSERT_TRUE(t.update(6001) == SendTracker::State::Failed);
  bool ok = true;
  TEST_ASSERT_TRUE(t.takeResult(ok));
  TEST_ASSERT_FALSE(ok);
}

static void test_a_late_acknowledgement_is_ignored() {
  SendTracker t;
  t.begin(0, {7});
  TEST_ASSERT_TRUE(t.update(5001) == SendTracker::State::Failed);
  t.onAck(7, 5100);
  TEST_ASSERT_TRUE(t.state() == SendTracker::State::Failed);
  // Even if the timeout has not been noticed yet, an ack that arrives late does not rescue the send.
  SendTracker u;
  u.begin(0, {9});
  u.onAck(9, 5001);
  TEST_ASSERT_TRUE(u.state() == SendTracker::State::Failed);
  // An ack at 5000 ms is in time.
  SendTracker w;
  w.begin(0, {9});
  w.onAck(9, 5000);
  TEST_ASSERT_TRUE(w.state() == SendTracker::State::Succeeded);
}

static void test_no_groups_fails_at_once() {
  SendTracker t;
  t.begin(100, {});
  TEST_ASSERT_TRUE(t.state() == SendTracker::State::Failed);
  bool ok = true;
  TEST_ASSERT_TRUE(t.takeResult(ok));
  TEST_ASSERT_FALSE(ok);
}

static void test_fail_now_and_stray_acks() {
  SendTracker t;
  t.begin(0, {1, 2});
  t.failNow();
  TEST_ASSERT_TRUE(t.state() == SendTracker::State::Failed);
  t.onAck(1, 10);
  t.onAck(2, 10);
  TEST_ASSERT_TRUE(t.state() == SendTracker::State::Failed);

  SendTracker u;
  u.begin(0, {5});
  u.onAck(99, 10);  // not one of ours
  u.onAck(5, 20);
  u.onAck(5, 30);  // duplicate
  TEST_ASSERT_TRUE(u.state() == SendTracker::State::Succeeded);
  SendTracker idle;
  idle.onAck(1, 0);
  TEST_ASSERT_TRUE(idle.state() == SendTracker::State::Idle);
}

static void test_tracker_survives_millis_wraparound() {
  SendTracker t;
  t.begin(0xFFFFF000u, {1});
  TEST_ASSERT_TRUE(t.update(0x00000100u) == SendTracker::State::Pending);  // 4.4 s later, across the wrap
  t.onAck(1, 0x00000200u);
  TEST_ASSERT_TRUE(t.state() == SendTracker::State::Succeeded);
}

// ---- 6.1 device_id ----

static void test_device_id_from_mac() {
  TEST_ASSERT_EQUAL_STRING("d-3c71bf4a2b10", deviceIdFromMac(0x3C71BF4A2B10ULL).c_str());
  TEST_ASSERT_EQUAL_STRING("d-0000000000ab", deviceIdFromMac(0xABULL).c_str());
  TEST_ASSERT_EQUAL_STRING("d-000000000000", deviceIdFromMac(0).c_str());
  TEST_ASSERT_EQUAL_STRING("d-0a0b0c0d0e0f", deviceIdFromMac(0x0A0B0C0D0E0FULL).c_str());
  TEST_ASSERT_EQUAL_STRING("d-ffffffffffff", deviceIdFromMac(0xFFFFFFFFFFFFFFFFULL).c_str());  // bits above 48 ignored
  const uint8_t mac[6] = {0x3C, 0x71, 0xBF, 0x4A, 0x2B, 0x10};
  TEST_ASSERT_EQUAL_STRING("d-3c71bf4a2b10", deviceIdFromMacBytes(mac).c_str());
  const uint8_t lead[6] = {0x00, 0x00, 0x1A, 0x02, 0x00, 0x0F};
  TEST_ASSERT_EQUAL_STRING("d-00001a02000f", deviceIdFromMacBytes(lead).c_str());
}

static void test_device_suffix_and_validation() {
  TEST_ASSERT_EQUAL_STRING("4a2b10", deviceSuffix("d-3c71bf4a2b10").c_str());
  TEST_ASSERT_EQUAL_STRING("000001", deviceSuffix("d-000000000001").c_str());
  TEST_ASSERT_EQUAL_STRING("", deviceSuffix("d-3c71bf4a2b1").c_str());
  TEST_ASSERT_EQUAL_STRING("", deviceSuffix("").c_str());
  TEST_ASSERT_TRUE(isValidDeviceId("d-3c71bf4a2b10"));
  TEST_ASSERT_TRUE(isValidDeviceId("d-000000000001"));
  const char* bad[] = {"", "d-", "d-3C71BF4A2B10", "d-3c71bf4a2b1", "d-3c71bf4a2b100", "x-3c71bf4a2b10", "d_3c71bf4a2b10", "d-3c71bf4a2b1g", " d-3c71bf4a2b10"};
  for (const char* b : bad) TEST_ASSERT_FALSE_MESSAGE(isValidDeviceId(b), b);
}

void run_messages_tests() {
  RUN_TEST(test_press_encodes_to_the_documented_wire_format);
  RUN_TEST(test_press_without_a_synchronised_clock_has_no_timestamp);
  RUN_TEST(test_press_round_trips);
  RUN_TEST(test_press_from_newer_firmware_is_understood);
  RUN_TEST(test_garbage_and_truncated_payloads_are_rejected_without_crashing);
  RUN_TEST(test_a_missing_or_invalid_sender_is_rejected);
  RUN_TEST(test_a_missing_message_id_is_rejected);
  RUN_TEST(test_missing_pattern_and_song_get_defaults);
  RUN_TEST(test_invalid_colors_become_warm_white);
  RUN_TEST(test_invalid_speed_defaults_to_medium);
  RUN_TEST(test_invalid_or_overlong_song_becomes_the_default_chime);
  RUN_TEST(test_exactly_512_characters_of_song_is_accepted);
  RUN_TEST(test_timestamp_handling);
  RUN_TEST(test_long_names_are_trimmed_and_capped);
  RUN_TEST(test_status_payloads_are_exactly_as_documented);
  RUN_TEST(test_config_decodes);
  RUN_TEST(test_config_round_trips_through_encode);
  RUN_TEST(test_empty_group_list_is_valid);
  RUN_TEST(test_config_ignores_unknown_fields_and_defaults_the_channel);
  RUN_TEST(test_invalid_config_is_rejected_whole);
  RUN_TEST(test_group_name_length_limits);
  RUN_TEST(test_duplicate_groups_collapse_and_too_many_are_rejected);
  RUN_TEST(test_diff_adding_a_group);
  RUN_TEST(test_diff_removing_a_group);
  RUN_TEST(test_diff_channel_change_and_no_change_and_empty);
  RUN_TEST(test_previous_membership_is_kept_when_a_new_config_is_invalid);
  RUN_TEST(test_group_and_channel_validators);
  RUN_TEST(test_all_copies_acknowledged_is_success);
  RUN_TEST(test_one_missing_acknowledgement_fails_after_five_seconds);
  RUN_TEST(test_a_late_acknowledgement_is_ignored);
  RUN_TEST(test_no_groups_fails_at_once);
  RUN_TEST(test_fail_now_and_stray_acks);
  RUN_TEST(test_tracker_survives_millis_wraparound);
  RUN_TEST(test_device_id_from_mac);
  RUN_TEST(test_device_suffix_and_validation);
}
