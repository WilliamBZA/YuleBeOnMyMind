// Tests for settings_model (6.2), settings_validation (6.3), tz_table (6.4) and settings_api (15.2).
#include <unity.h>

#include <ArduinoJson.h>

#include <set>
#include <string>

#include "pattern.h"
#include "settings_api.h"
#include "settings_model.h"
#include "settings_validation.h"
#include "song_library.h"
#include "tz_table.h"

using namespace yule;

namespace {

SettingsUpdate applyJson(const Profile& current, const std::string& json) { return applySettingsJson(current, json); }

std::string customSong(size_t chars) {
  std::string t = "T:d=4,o=5,b=120:c";
  while (t.size() + 2 <= chars) t += ",c";
  return t;
}

}  // namespace

// ---- 6.2 settings_model ----

static void test_default_profile() {
  const Profile p = defaultProfile();
  TEST_ASSERT_EQUAL_STRING("Family member", p.displayName.c_str());
  TEST_ASSERT_TRUE(p.song.kind == SongChoice::Kind::Library);
  TEST_ASSERT_EQUAL_STRING(carolAt(0).id, p.song.libraryId.c_str());
  TEST_ASSERT_EQUAL_STRING(carolAt(0).rtttl, resolveSongRtttl(p.song).c_str());
  TEST_ASSERT_TRUE(p.pattern.effect == Effect::Breathe);
  TEST_ASSERT_EQUAL_UINT8(1, p.pattern.colorCount);
  TEST_ASSERT_TRUE(p.pattern.colors[0] == kWarmWhite);
  TEST_ASSERT_EQUAL_UINT8(2, p.pattern.speed);
  TEST_ASSERT_EQUAL_UINT8(50, p.brightness);
  TEST_ASSERT_FALSE(p.muted);
  TEST_ASSERT_FALSE(p.quiet.enabled);
  TEST_ASSERT_EQUAL_STRING("UTC", p.timeZone.name.c_str());
  TEST_ASSERT_EQUAL_STRING("UTC0", p.timeZone.rule.c_str());
}

static void test_profile_round_trips() {
  Profile p = defaultProfile();
  p.displayName = "Gran";
  p.song.kind = SongChoice::Kind::Custom;
  p.song.rtttl = "Tune:d=4,o=5,b=100:c,e,g,2c6";
  p.pattern.effect = Effect::CandyCane;
  p.pattern.colorCount = 2;
  p.pattern.colors[0] = Rgb(255, 0, 0);
  p.pattern.colors[1] = Rgb(255, 255, 255);
  p.pattern.speed = 3;
  p.brightness = 20;
  p.muted = true;
  p.quiet.enabled = true;
  p.quiet.startMinute = 22 * 60;
  p.quiet.endMinute = 6 * 60 + 45;
  p.timeZone.name = "Europe/London";
  p.timeZone.rule = "GMT0BST,M3.5.0/1,M10.5.0";
  const Profile back = decodeProfile(encodeProfile(p));
  TEST_ASSERT_TRUE(back == p);
  // A library song round-trips too.
  Profile q = defaultProfile();
  q.song.libraryId = "silent-night";
  TEST_ASSERT_TRUE(decodeProfile(encodeProfile(q)) == q);
  TEST_ASSERT_TRUE(encodeProfile(p).find("\"schema\":1") != std::string::npos);
}

static void test_an_older_blob_gets_defaults_for_missing_fields() {
  const Profile d = defaultProfile();
  Profile p = decodeProfile("{\"schema\":0,\"name\":\"Old name\",\"brightness\":80}");
  TEST_ASSERT_EQUAL_STRING("Old name", p.displayName.c_str());
  TEST_ASSERT_EQUAL_UINT8(80, p.brightness);
  TEST_ASSERT_TRUE(p.pattern == d.pattern);
  TEST_ASSERT_EQUAL_STRING(d.song.libraryId.c_str(), p.song.libraryId.c_str());
  TEST_ASSERT_FALSE(p.muted);
  TEST_ASSERT_FALSE(p.quiet.enabled);
  TEST_ASSERT_EQUAL_STRING("UTC0", p.timeZone.rule.c_str());
  // No schema at all, and a newer schema with extra fields, both load.
  TEST_ASSERT_EQUAL_STRING("X", decodeProfile("{\"name\":\"X\"}").displayName.c_str());
  TEST_ASSERT_EQUAL_STRING("Future", decodeProfile("{\"schema\":9,\"name\":\"Future\",\"hologram\":true}").displayName.c_str());
}

static void test_damaged_profiles_fall_back_to_defaults() {
  const Profile d = defaultProfile();
  for (const char* junk : {"", "garbage", "[]", "42", "null", "{", "{\"name\":"}) TEST_ASSERT_TRUE_MESSAGE(decodeProfile(junk) == d, junk);
  const Profile p = decodeProfile(
      "{\"name\":\"\",\"song\":{\"kind\":\"library\",\"id\":\"no-such\"},\"pattern\":{\"effect\":\"plasma\",\"colors\":[\"red\"],\"speed\":9},"
      "\"brightness\":\"bright\",\"muted\":\"yes\",\"quiet\":{\"start\":\"25:00\"},\"tz\":{\"rule\":\"\"}}");
  TEST_ASSERT_TRUE(p == d);
  TEST_ASSERT_EQUAL_UINT8(100, decodeProfile("{\"brightness\":1000}").brightness);  // out-of-range numbers are clamped
  TEST_ASSERT_EQUAL_UINT8(5, decodeProfile("{\"brightness\":0}").brightness);
}

static void test_unknown_song_choice_still_plays_something() {
  SongChoice s;
  s.libraryId = "gone";
  TEST_ASSERT_EQUAL_STRING(carolAt(0).rtttl, resolveSongRtttl(s).c_str());
  s.kind = SongChoice::Kind::Custom;
  s.rtttl = "";
  TEST_ASSERT_EQUAL_STRING(carolAt(0).rtttl, resolveSongRtttl(s).c_str());
  s.rtttl = "T:d=4:c";
  TEST_ASSERT_EQUAL_STRING("T:d=4:c", resolveSongRtttl(s).c_str());
}

static void test_ident_round_trips_and_defaults() {
  Ident i;
  i.password = "p4ssw0rd-p4ssw0rd-p4ss";
  i.brokerHost = "broker.example.org";
  i.brokerPort = 18883;
  const Ident back = decodeIdent(encodeIdent(i));
  TEST_ASSERT_EQUAL_STRING(i.password.c_str(), back.password.c_str());
  TEST_ASSERT_EQUAL_STRING(i.brokerHost.c_str(), back.brokerHost.c_str());
  TEST_ASSERT_EQUAL_UINT16(18883, back.brokerPort);
  TEST_ASSERT_TRUE(back.hasPassword());
  const Ident empty = decodeIdent("");
  TEST_ASSERT_FALSE(empty.hasPassword());
  TEST_ASSERT_EQUAL_UINT16(0, empty.brokerPort);
  TEST_ASSERT_EQUAL_UINT16(0, decodeIdent("{\"port\":70000}").brokerPort);
}

static void test_the_profile_json_never_carries_the_password() {
  Profile p = defaultProfile();
  Ident i;
  i.password = "secret-secret-secret";
  const std::string json = encodeProfile(p);
  TEST_ASSERT_TRUE(json.find("secret") == std::string::npos);
  TEST_ASSERT_TRUE(json.find("password") == std::string::npos);
}

// ---- 6.3 validators ----

static void test_display_name_rules() {
  const Profile cur = defaultProfile();
  SettingsUpdate u = applyJson(cur, "{\"name\":\"Gran\"}");
  TEST_ASSERT_TRUE(u.applied());
  TEST_ASSERT_EQUAL_STRING("Gran", u.profile.displayName.c_str());
  u = applyJson(cur, "{\"name\":\"   Gran   \"}");
  TEST_ASSERT_EQUAL_STRING("Gran", u.profile.displayName.c_str());  // trimmed
  u = applyJson(cur, "{\"name\":\"" + std::string(24, 'n') + "\"}");
  TEST_ASSERT_TRUE(u.applied());
  u = applyJson(cur, "{\"name\":\"" + std::string(25, 'n') + "\"}");
  TEST_ASSERT_FALSE(u.applied());
  TEST_ASSERT_TRUE(u.validation.hasError("name"));
  TEST_ASSERT_TRUE(u.profile == cur);  // nothing changed
  u = applyJson(cur, "{\"name\":\"    \"}");
  TEST_ASSERT_TRUE(u.validation.hasError("name"));
  u = applyJson(cur, "{\"name\":\"\"}");
  TEST_ASSERT_TRUE(u.validation.hasError("name"));
  u = applyJson(cur, "{\"name\":42}");
  TEST_ASSERT_TRUE(u.validation.hasError("name"));
  // Characters, not bytes: 24 two-byte characters fit.
  std::string accented;
  for (int i = 0; i < 24; ++i) accented += "\xC3\xA9";
  TEST_ASSERT_TRUE(applyJson(cur, "{\"name\":\"" + accented + "\"}").applied());
  TEST_ASSERT_FALSE(applyJson(cur, "{\"name\":\"" + accented + "\xC3\xA9\"}").applied());
}

static void test_song_selection_rules() {
  const Profile cur = defaultProfile();
  SettingsUpdate u = applyJson(cur, "{\"song\":{\"kind\":\"library\",\"id\":\"jingle-bells\"}}");
  TEST_ASSERT_TRUE(u.applied());
  TEST_ASSERT_EQUAL_STRING("jingle-bells", u.profile.song.libraryId.c_str());
  u = applyJson(cur, "{\"song\":{\"kind\":\"library\",\"id\":\"nope\"}}");
  TEST_ASSERT_TRUE(u.validation.hasError("song"));

  // A valid custom tune.
  u = applyJson(cur, "{\"song\":{\"kind\":\"custom\",\"rtttl\":\"Tune:d=4,o=5,b=120:c,e,g\"}}");
  TEST_ASSERT_TRUE(u.applied());
  TEST_ASSERT_TRUE(u.profile.song.kind == SongChoice::Kind::Custom);
  TEST_ASSERT_EQUAL_STRING("Tune:d=4,o=5,b=120:c,e,g", u.profile.song.rtttl.c_str());
  TEST_ASSERT_TRUE(u.validation.warnings.empty());

  // Exactly 512 characters is allowed, 513 is not.
  u = applyJson(cur, "{\"song\":{\"kind\":\"custom\",\"rtttl\":\"" + customSong(512) + "\"}}");
  TEST_ASSERT_TRUE_MESSAGE(u.applied(), "512 characters");
  u = applyJson(cur, "{\"song\":{\"kind\":\"custom\",\"rtttl\":\"" + customSong(514) + "\"}}");
  TEST_ASSERT_TRUE(u.validation.hasError("song"));
  TEST_ASSERT_TRUE(u.profile == cur);
}

static void test_invalid_custom_tune_explains_where_and_keeps_the_previous_song() {
  Profile cur = defaultProfile();
  cur.song.libraryId = "silent-night";
  const std::string bad = "Test:d=4,o=5,b=120:c,h,e";
  const SettingsUpdate u = applyJson(cur, "{\"song\":{\"kind\":\"custom\",\"rtttl\":\"" + bad + "\"}}");
  TEST_ASSERT_FALSE(u.applied());
  TEST_ASSERT_TRUE(u.validation.hasError("song"));
  const std::string expectedPos = std::to_string(bad.find('h') + 1);
  TEST_ASSERT_TRUE_MESSAGE(u.validation.errors[0].message.find(expectedPos) != std::string::npos,
                           u.validation.errors[0].message.c_str());
  TEST_ASSERT_EQUAL_STRING("silent-night", u.profile.song.libraryId.c_str());
  TEST_ASSERT_TRUE(u.profile.song.kind == SongChoice::Kind::Library);
}

static void test_a_45_second_tune_is_saved_with_a_warning() {
  std::string tune = "Long:d=4,o=5,b=120:";
  for (int i = 0; i < 90; ++i) tune += "c,";  // 90 x 0.5 s = 45 s
  const SettingsUpdate u = applyJson(defaultProfile(), "{\"song\":{\"kind\":\"custom\",\"rtttl\":\"" + tune + "\"}}");
  TEST_ASSERT_TRUE(u.applied());
  TEST_ASSERT_TRUE(u.validation.hasWarning("song"));
  TEST_ASSERT_TRUE(u.validation.warnings[0].message.find("30 seconds") != std::string::npos);
  TEST_ASSERT_TRUE(u.validation.warnings[0].message.find("45") != std::string::npos);
  // Exactly 30 s: no warning.
  std::string thirty = "T:d=4,o=5,b=120:";
  for (int i = 0; i < 60; ++i) thirty += "c,";
  TEST_ASSERT_TRUE(applyJson(defaultProfile(), "{\"song\":{\"kind\":\"custom\",\"rtttl\":\"" + thirty + "\"}}").validation.warnings.empty());
}

static void test_pattern_rules() {
  const Profile cur = defaultProfile();
  SettingsUpdate u = applyJson(cur, "{\"pattern\":{\"effect\":\"twinkle\",\"colors\":[\"#FF0000\",\"#00FF00\"],\"speed\":2}}");
  TEST_ASSERT_TRUE(u.applied());
  TEST_ASSERT_TRUE(u.profile.pattern.effect == Effect::Twinkle);
  TEST_ASSERT_EQUAL_UINT8(2, u.profile.pattern.colorCount);
  TEST_ASSERT_TRUE(u.profile.pattern.colors[1] == Rgb(0, 255, 0));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"pattern\":{\"effect\":\"plasma\"}}").validation.hasError("pattern.effect"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"pattern\":{\"effect\":7}}").validation.hasError("pattern.effect"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"pattern\":{\"colors\":[]}}").validation.hasError("pattern.colors"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"pattern\":{\"colors\":[\"#111111\",\"#222222\",\"#333333\",\"#444444\"]}}").validation.hasError("pattern.colors"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"pattern\":{\"colors\":[\"red\"]}}").validation.hasError("pattern.colors"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"pattern\":{\"colors\":[\"#FF0000\",5]}}").validation.hasError("pattern.colors"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"pattern\":{\"colors\":\"#FF0000\"}}").validation.hasError("pattern.colors"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"pattern\":{\"speed\":0}}").validation.hasError("pattern.speed"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"pattern\":{\"speed\":4}}").validation.hasError("pattern.speed"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"pattern\":{\"speed\":\"fast\"}}").validation.hasError("pattern.speed"));
  for (int s = 1; s <= 3; ++s) TEST_ASSERT_TRUE(applyJson(cur, "{\"pattern\":{\"speed\":" + std::to_string(s) + "}}").applied());
  // Three colours is the maximum and works.
  TEST_ASSERT_TRUE(applyJson(cur, "{\"pattern\":{\"colors\":[\"#111111\",\"#222222\",\"#333333\"]}}").applied());
  // Partial: changing only the speed keeps the effect and colours.
  u = applyJson(cur, "{\"pattern\":{\"speed\":3}}");
  TEST_ASSERT_TRUE(u.profile.pattern.effect == cur.pattern.effect);
  TEST_ASSERT_TRUE(u.profile.pattern.colors[0] == cur.pattern.colors[0]);
}

static void test_brightness_rules() {
  const Profile cur = defaultProfile();
  for (int ok : {5, 20, 50, 100}) {
    const SettingsUpdate u = applyJson(cur, "{\"brightness\":" + std::to_string(ok) + "}");
    TEST_ASSERT_TRUE(u.applied());
    TEST_ASSERT_EQUAL_UINT8(ok, u.profile.brightness);
  }
  for (int bad : {-1, 0, 3, 4, 101, 255, 1000}) {
    const SettingsUpdate u = applyJson(cur, "{\"brightness\":" + std::to_string(bad) + "}");
    TEST_ASSERT_TRUE_MESSAGE(u.validation.hasError("brightness"), std::to_string(bad).c_str());
    TEST_ASSERT_EQUAL_UINT8(50, u.profile.brightness);
  }
  TEST_ASSERT_TRUE(applyJson(cur, "{\"brightness\":\"max\"}").validation.hasError("brightness"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"brightness\":50.5}").validation.hasError("brightness"));
}

static void test_mute_and_quiet_hours_rules() {
  const Profile cur = defaultProfile();
  SettingsUpdate u = applyJson(cur, "{\"muted\":true,\"quiet\":{\"enabled\":true,\"start\":\"21:30\",\"end\":\"07:00\"}}");
  TEST_ASSERT_TRUE(u.applied());
  TEST_ASSERT_TRUE(u.profile.muted);
  TEST_ASSERT_TRUE(u.profile.quiet.enabled);
  TEST_ASSERT_EQUAL_INT(21 * 60 + 30, u.profile.quiet.startMinute);
  TEST_ASSERT_EQUAL_INT(7 * 60, u.profile.quiet.endMinute);
  TEST_ASSERT_TRUE(applyJson(cur, "{\"muted\":\"yes\"}").validation.hasError("muted"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"quiet\":{\"start\":\"25:00\"}}").validation.hasError("quiet.start"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"quiet\":{\"end\":\"7am\"}}").validation.hasError("quiet.end"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"quiet\":{\"enabled\":\"on\"}}").validation.hasError("quiet.enabled"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"quiet\":{\"start\":\"22:00\",\"end\":\"22:00\"}}").applied());  // equal = no quiet hours, allowed
  // Changing just one time keeps the other.
  u = applyJson(u.profile, "{\"quiet\":{\"start\":\"22:15\"}}");
  TEST_ASSERT_TRUE(u.applied());
  TEST_ASSERT_EQUAL_INT(22 * 60 + 15, u.profile.quiet.startMinute);
  TEST_ASSERT_EQUAL_INT(7 * 60, u.profile.quiet.endMinute);
}

static void test_time_zone_rules() {
  const Profile cur = defaultProfile();
  SettingsUpdate u = applyJson(cur, "{\"tz\":{\"name\":\"Europe/London\",\"rule\":\"GMT0BST,M3.5.0/1,M10.5.0\"}}");
  TEST_ASSERT_TRUE(u.applied());
  TEST_ASSERT_EQUAL_STRING("Europe/London", u.profile.timeZone.name.c_str());
  // A custom POSIX rule.
  u = applyJson(cur, "{\"tz\":{\"name\":\"custom\",\"rule\":\"<+0545>-5:45\"}}");
  TEST_ASSERT_TRUE(u.applied());
  TEST_ASSERT_EQUAL_STRING("<+0545>-5:45", u.profile.timeZone.rule.c_str());
  // A menu zone without a rule takes the table's rule.
  u = applyJson(cur, "{\"tz\":{\"name\":\"Asia/Tokyo\"}}");
  TEST_ASSERT_TRUE(u.applied());
  TEST_ASSERT_EQUAL_STRING("JST-9", u.profile.timeZone.rule.c_str());
  // An empty rule with an unknown zone is an error.
  TEST_ASSERT_TRUE(applyJson(cur, "{\"tz\":{\"name\":\"custom\",\"rule\":\"\"}}").validation.hasError("tz.rule"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"tz\":{\"name\":\"Mars/Olympus\",\"rule\":\"   \"}}").validation.hasError("tz.rule"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"tz\":{\"rule\":\"" + std::string(64, 'x') + "\"}}").validation.hasError("tz.rule"));
  TEST_ASSERT_TRUE(applyJson(cur, "{\"tz\":{\"rule\":\"UTC\\n0\"}}").validation.hasError("tz.rule"));  // control character inside
  TEST_ASSERT_TRUE(applyJson(cur, "{\"tz\":{\"rule\":\"  UTC0\\n\"}}").applied());                  // surrounding whitespace is trimmed
  TEST_ASSERT_TRUE(applyJson(cur, "{\"tz\":\"Europe/London\"}").validation.hasError("tz.rule"));
}

static void test_one_invalid_field_changes_nothing() {
  Profile cur = defaultProfile();
  cur.displayName = "Gran";
  const SettingsUpdate u = applyJson(cur, "{\"name\":\"Mum\",\"brightness\":3,\"muted\":true}");
  TEST_ASSERT_FALSE(u.applied());
  TEST_ASSERT_TRUE(u.profile == cur);
  TEST_ASSERT_EQUAL_UINT32(1, u.validation.errors.size());
}

static void test_several_errors_are_all_reported() {
  const SettingsUpdate u = applyJson(defaultProfile(), "{\"name\":\"\",\"brightness\":3,\"pattern\":{\"speed\":9}}");
  TEST_ASSERT_TRUE(u.validation.hasError("name"));
  TEST_ASSERT_TRUE(u.validation.hasError("brightness"));
  TEST_ASSERT_TRUE(u.validation.hasError("pattern.speed"));
  const std::string json = encodeValidation(u.validation);
  TEST_ASSERT_TRUE(json.find("\"ok\":false") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"field\":\"brightness\"") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"warnings\":[]") != std::string::npos);
}

static void test_a_valid_save_reports_ok() {
  const SettingsUpdate u = applyJson(defaultProfile(), "{\"name\":\"Gran\"}");
  const std::string json = encodeValidation(u.validation);
  TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"errors\":[],\"warnings\":[]}", json.c_str());
}

static void test_unusable_request_bodies_are_rejected() {
  const Profile cur = defaultProfile();
  for (const char* junk : {"", "garbage", "[]", "42", "{", "null"}) {
    const SettingsUpdate u = applyJson(cur, junk);
    TEST_ASSERT_FALSE_MESSAGE(u.applied(), junk);
    TEST_ASSERT_TRUE(u.profile == cur);
  }
  TEST_ASSERT_TRUE(applyJson(cur, "{}").applied());  // an empty patch is a valid no-op
  TEST_ASSERT_TRUE(applyJson(cur, "{\"hologram\":true}").applied());  // unknown fields are ignored
  TEST_ASSERT_FALSE(applyJson(cur, std::string(5000, ' ') + "{}").applied());  // oversized
}

static void test_a_saved_change_applies_to_the_next_message() {
  // The stored profile after a valid save is exactly what encodeProfile/decodeProfile will reload.
  const SettingsUpdate u = applyJson(defaultProfile(), "{\"song\":{\"kind\":\"library\",\"id\":\"deck-the-halls\"},\"brightness\":30}");
  TEST_ASSERT_TRUE(u.applied());
  const Profile reloaded = decodeProfile(encodeProfile(u.profile));
  TEST_ASSERT_TRUE(reloaded == u.profile);
  TEST_ASSERT_EQUAL_STRING(findCarol("deck-the-halls")->rtttl, resolveSongRtttl(reloaded.song).c_str());
}

static void test_device_setup_rules() {
  Ident cur;
  cur.password = "stored-password-1234";
  DeviceUpdate d = applyDeviceJson(cur, "{\"password\":\"new-password-abcdefgh\",\"host\":\"broker.example.org\",\"port\":18883}");
  TEST_ASSERT_TRUE(d.applied());
  TEST_ASSERT_EQUAL_STRING("new-password-abcdefgh", d.ident.password.c_str());
  TEST_ASSERT_EQUAL_STRING("broker.example.org", d.ident.brokerHost.c_str());
  TEST_ASSERT_EQUAL_UINT16(18883, d.ident.brokerPort);

  // An absent or empty password keeps the stored one (the page never shows it).
  d = applyDeviceJson(cur, "{\"host\":\"b.example.org\"}");
  TEST_ASSERT_TRUE(d.applied());
  TEST_ASSERT_EQUAL_STRING("stored-password-1234", d.ident.password.c_str());
  d = applyDeviceJson(cur, "{\"password\":\"\",\"port\":0}");
  TEST_ASSERT_TRUE(d.applied());
  TEST_ASSERT_EQUAL_STRING("stored-password-1234", d.ident.password.c_str());

  // Empty host and port 0 mean "use the defaults".
  Ident withOverride = cur;
  withOverride.brokerHost = "old.example.org";
  withOverride.brokerPort = 1234;
  d = applyDeviceJson(withOverride, "{\"host\":\"\",\"port\":0}");
  TEST_ASSERT_TRUE(d.applied());
  TEST_ASSERT_EQUAL_STRING("", d.ident.brokerHost.c_str());
  TEST_ASSERT_EQUAL_UINT16(0, d.ident.brokerPort);

  // Errors leave everything unchanged.
  for (const char* bad : {"{\"host\":\"has space\"}", "{\"host\":\"-bad.example.org\"}", "{\"host\":\"a..b\"}", "{\"host\":5}",
                          "{\"port\":65536}", "{\"port\":-1}", "{\"port\":\"x\"}", "{\"password\":\"bad\\u0001pw\"}",
                          "{\"password\":5}", "{\"password\":\"" "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" "\"}",
                          "garbage", ""}) {
    d = applyDeviceJson(cur, bad);
    TEST_ASSERT_FALSE_MESSAGE(d.applied(), bad);
    TEST_ASSERT_EQUAL_STRING(cur.password.c_str(), d.ident.password.c_str());
  }
  TEST_ASSERT_TRUE(applyDeviceJson(cur, "{\"host\":\"192.168.1.20\",\"port\":8883}").applied());
  TEST_ASSERT_TRUE(applyDeviceJson(cur, "{\"port\":65535}").applied());
}

// ---- 6.4 tz_table ----

static void test_time_zone_table_is_sound() {
  TEST_ASSERT_TRUE(timeZoneCount() >= 40);
  std::set<std::string> names;
  for (size_t i = 0; i < timeZoneCount(); ++i) {
    const TimeZoneInfo& z = timeZoneAt(i);
    TEST_ASSERT_TRUE_MESSAGE(std::string(z.name).size() > 0, "name");
    TEST_ASSERT_TRUE_MESSAGE(std::string(z.rule).size() > 0, z.name);
    TEST_ASSERT_TRUE_MESSAGE(std::string(z.rule).size() <= 63, z.name);
    TEST_ASSERT_TRUE_MESSAGE(names.insert(z.name).second, (std::string("duplicate name ") + z.name).c_str());
    // Each rule must be acceptable to the validator.
    ValidationResult v;
    TEST_ASSERT_TRUE_MESSAGE(validateTimeZoneRule(z.rule, v), z.name);
  }
}

static void test_time_zone_lookup() {
  const TimeZoneInfo* london = findTimeZone("Europe/London");
  TEST_ASSERT_NOT_NULL(london);
  TEST_ASSERT_EQUAL_STRING("GMT0BST,M3.5.0/1,M10.5.0", london->rule);
  TEST_ASSERT_NOT_NULL(findTimeZone("America/New_York"));
  TEST_ASSERT_NOT_NULL(findTimeZone("Australia/Sydney"));
  TEST_ASSERT_NULL(findTimeZone("Europe/Atlantis"));
  TEST_ASSERT_NULL(findTimeZone(""));
  TEST_ASSERT_EQUAL_STRING("UTC", defaultTimeZone().name);
  TEST_ASSERT_EQUAL_STRING("UTC0", defaultTimeZone().rule);
}

// ---- 15.2 settings API bodies ----

static JsonDocument parsed(const std::string& json) {
  JsonDocument doc;
  TEST_ASSERT_FALSE_MESSAGE(deserializeJson(doc, json), json.c_str());
  return doc;
}

static void test_library_lists_every_song_effect_and_zone() {
  const JsonDocument doc = parsed(encodeLibrary());
  TEST_ASSERT_EQUAL_UINT32(carolCount(), doc["songs"].size());
  TEST_ASSERT_EQUAL_STRING(defaultCarol().id, doc["songs"][0]["id"]);
  TEST_ASSERT_EQUAL_STRING(defaultCarol().id, doc["defaultSong"]);
  for (JsonVariantConst song : doc["songs"].as<JsonArrayConst>()) {
    TEST_ASSERT_TRUE(song["ms"].as<uint32_t>() > 0);
    TEST_ASSERT_TRUE(song["ms"].as<uint32_t>() <= 30000);
    TEST_ASSERT_NOT_NULL(findCarol(song["id"].as<const char*>()));
  }
  TEST_ASSERT_EQUAL_UINT32(kEffectCount, doc["effects"].size());
  TEST_ASSERT_EQUAL_STRING("candy-cane", doc["effects"][static_cast<size_t>(Effect::CandyCane)]["name"]);
  TEST_ASSERT_EQUAL_INT(2, doc["effects"][static_cast<size_t>(Effect::CandyCane)]["colors"].as<int>());
  TEST_ASSERT_EQUAL_INT(0, doc["effects"][static_cast<size_t>(Effect::Rainbow)]["colors"].as<int>());
  TEST_ASSERT_EQUAL_UINT32(timeZoneCount(), doc["timeZones"].size());
  TEST_ASSERT_EQUAL_INT(24, doc["limits"]["nameChars"].as<int>());
  TEST_ASSERT_EQUAL_INT(512, doc["limits"]["songChars"].as<int>());
}

static void test_status_lists_the_device_id_and_group_names() {
  StatusView s;
  s.deviceId = "d-3c71bf4a2b10";
  s.groupNames = {"Brander Grandparents", "Smith \"the elder\" Grandparents"};
  s.channel = "beta";
  s.setupRequired = false;
  s.update.state = UpdateView::State::Downloading;
  s.update.percent = 42;
  s.update.version = "0.3.0";
  const JsonDocument doc = parsed(encodeStatus(s));
  TEST_ASSERT_EQUAL_STRING("d-3c71bf4a2b10", doc["id"]);
  TEST_ASSERT_EQUAL_UINT32(2, doc["groups"].size());
  TEST_ASSERT_EQUAL_STRING("Smith \"the elder\" Grandparents", doc["groups"][1]);
  TEST_ASSERT_EQUAL_STRING("beta", doc["channel"]);
  TEST_ASSERT_EQUAL_STRING("downloading", doc["update"]["state"]);
  TEST_ASSERT_EQUAL_INT(42, doc["update"]["percent"].as<int>());
  TEST_ASSERT_EQUAL_STRING("0.3.0", doc["update"]["version"]);
}

static void test_status_of_a_new_device() {
  StatusView s;
  s.deviceId = "d-000000000001";
  s.setupRequired = true;
  s.timeSynced = false;
  s.localTime = "Thu 1970-01-01 00:00";
  const JsonDocument doc = parsed(encodeStatus(s));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, doc["groups"].size(), "not in any family group yet");
  TEST_ASSERT_TRUE(doc["setupRequired"].as<bool>());
  TEST_ASSERT_FALSE(doc["clock"]["synced"].as<bool>());
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", doc["clock"]["local"], "an unsynchronised time is not shown");
  TEST_ASSERT_EQUAL_STRING("up-to-date", doc["update"]["state"]);
  TEST_ASSERT_TRUE(doc["update"]["percent"].isNull());
}

static void test_update_state_names() {
  TEST_ASSERT_EQUAL_STRING("installed", updateStateName(UpdateView::State::Installed));
  TEST_ASSERT_EQUAL_STRING("failed", updateStateName(UpdateView::State::Failed));
  TEST_ASSERT_EQUAL_STRING("rolled-back", updateStateName(UpdateView::State::RolledBack));
  TEST_ASSERT_EQUAL_STRING("waiting", updateStateName(UpdateView::State::Waiting));
}

static void test_get_responses_never_contain_the_password() {
  Ident ident;
  ident.password = "Zq8pW3xLmN7rT2vY5bK9cH4s";
  ident.brokerHost = "broker.example.org";
  ident.brokerPort = 18883;
  const DeviceUpdate saved = applyDeviceJson(Ident(), "{\"password\":\"Zq8pW3xLmN7rT2vY5bK9cH4s\"}");
  TEST_ASSERT_TRUE(saved.applied());
  StatusView s;
  s.deviceId = "d-3c71bf4a2b10";
  s.brokerHost = ident.brokerHost;
  s.brokerPort = ident.brokerPort;
  s.brokerOverrideHost = ident.brokerHost;
  s.brokerOverridePort = ident.brokerPort;
  for (const std::string& body : {encodeStatus(s), encodeProfile(defaultProfile()), encodeLibrary(),
                                  encodeValidation(saved.validation)}) {
    TEST_ASSERT_TRUE_MESSAGE(body.find(ident.password) == std::string::npos, body.c_str());
    TEST_ASSERT_TRUE(body.find("password") == std::string::npos);
  }
}

void run_settings_tests() {
  RUN_TEST(test_library_lists_every_song_effect_and_zone);
  RUN_TEST(test_status_lists_the_device_id_and_group_names);
  RUN_TEST(test_status_of_a_new_device);
  RUN_TEST(test_update_state_names);
  RUN_TEST(test_get_responses_never_contain_the_password);
  RUN_TEST(test_default_profile);
  RUN_TEST(test_profile_round_trips);
  RUN_TEST(test_an_older_blob_gets_defaults_for_missing_fields);
  RUN_TEST(test_damaged_profiles_fall_back_to_defaults);
  RUN_TEST(test_unknown_song_choice_still_plays_something);
  RUN_TEST(test_ident_round_trips_and_defaults);
  RUN_TEST(test_the_profile_json_never_carries_the_password);
  RUN_TEST(test_display_name_rules);
  RUN_TEST(test_song_selection_rules);
  RUN_TEST(test_invalid_custom_tune_explains_where_and_keeps_the_previous_song);
  RUN_TEST(test_a_45_second_tune_is_saved_with_a_warning);
  RUN_TEST(test_pattern_rules);
  RUN_TEST(test_brightness_rules);
  RUN_TEST(test_mute_and_quiet_hours_rules);
  RUN_TEST(test_time_zone_rules);
  RUN_TEST(test_one_invalid_field_changes_nothing);
  RUN_TEST(test_several_errors_are_all_reported);
  RUN_TEST(test_a_valid_save_reports_ok);
  RUN_TEST(test_unusable_request_bodies_are_rejected);
  RUN_TEST(test_a_saved_change_applies_to_the_next_message);
  RUN_TEST(test_device_setup_rules);
  RUN_TEST(test_time_zone_table_is_sound);
  RUN_TEST(test_time_zone_lookup);
}
