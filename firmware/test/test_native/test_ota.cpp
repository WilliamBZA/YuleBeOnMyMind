// Tests for ota_manifest (7.1) and ota_policy (7.2, 7.3).
#include <unity.h>

#include <string>

#include "ota_manifest.h"
#include "ota_policy.h"

using namespace yule;

namespace {

const std::string kSha(64, 'a');
const std::string kSig = std::string(70, 'A') + "==";  // 72 characters of padded base64

std::string manifestJson(const std::string& version = "0.3.0", const std::string& board = "esp32dev",
                         const std::string& url = "https://yule.williamb.net/firmware/yule-0.3.0-esp32dev.bin") {
  return "{\"v\":1,\"version\":\"" + version + "\",\"board\":\"" + board + "\",\"size\":1534016,\"sha256\":\"" + kSha +
         "\",\"sig\":\"" + kSig + "\",\"url\":\"" + url + "\"}";
}

OtaManifest manifestOf(const std::string& version, const std::string& board = "esp32dev") {
  const ManifestResult r = decodeManifest(manifestJson(version, board));
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error.c_str());
  return r.manifest;
}

InstallContext context(const std::string& running, bool idle = true) {
  InstallContext c;
  c.board = "esp32dev";
  c.runningVersion = running;
  c.idle = idle;
  return c;
}

}  // namespace

// ---- 7.1 ota_manifest ----

static void test_a_valid_manifest_decodes() {
  const ManifestResult r = decodeManifest(manifestJson());
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error.c_str());
  TEST_ASSERT_EQUAL_INT(1, r.manifest.v);
  TEST_ASSERT_EQUAL_STRING("0.3.0", r.manifest.version.c_str());
  TEST_ASSERT_EQUAL_STRING("esp32dev", r.manifest.board.c_str());
  TEST_ASSERT_EQUAL_UINT32(1534016, r.manifest.size);
  TEST_ASSERT_EQUAL_STRING(kSha.c_str(), r.manifest.sha256.c_str());
  TEST_ASSERT_EQUAL_STRING("https://yule.williamb.net/firmware/yule-0.3.0-esp32dev.bin", r.manifest.url.c_str());
  TEST_ASSERT_TRUE(r.manifest.signature.size() > 0);
}

static void test_each_missing_field_is_rejected() {
  const char* fields[] = {"v", "version", "board", "size", "sha256", "sig", "url"};
  for (const char* field : fields) {
    // Rebuild the manifest without one field.
    std::string json = "{";
    bool first = true;
    auto add = [&](const std::string& key, const std::string& value) {
      if (key == field) return;
      if (!first) json += ",";
      first = false;
      json += "\"" + key + "\":" + value;
    };
    add("v", "1");
    add("version", "\"0.3.0\"");
    add("board", "\"esp32dev\"");
    add("size", "1534016");
    add("sha256", "\"" + kSha + "\"");
    add("sig", "\"" + kSig + "\"");
    add("url", "\"https://yule.williamb.net/f.bin\"");
    json += "}";
    TEST_ASSERT_FALSE_MESSAGE(decodeManifest(json).ok, field);
  }
  // With every field present it is accepted.
  TEST_ASSERT_TRUE(decodeManifest("{\"v\":1,\"version\":\"0.3.0\",\"board\":\"esp32dev\",\"size\":10,\"sha256\":\"" + kSha +
                                  "\",\"sig\":\"" + kSig + "\",\"url\":\"https://yule.williamb.net/f.bin\"}").ok);
}

static void test_non_https_urls_are_rejected() {
  for (const char* url : {"http://yule.williamb.net/f.bin", "ftp://x/f.bin", "//yule.williamb.net/f.bin", "yule.williamb.net/f.bin",
                          "https://", "https:///f.bin", "HTTPS://yule.williamb.net/f.bin", "https://yule williamb.net/f.bin",
                          "javascript:alert(1)", ""}) {
    TEST_ASSERT_FALSE_MESSAGE(decodeManifest(manifestJson("0.3.0", "esp32dev", url)).ok, url);
  }
  TEST_ASSERT_TRUE(decodeManifest(manifestJson("0.3.0", "esp32dev", "https://192.168.1.5:8443/f.bin")).ok);
  TEST_ASSERT_FALSE(decodeManifest(manifestJson("0.3.0", "esp32dev", "https://x.example/" + std::string(300, 'a'))).ok);
}

static void test_unknown_fields_are_ignored() {
  const std::string json =
      "{\"v\":2,\"version\":\"0.3.0\",\"board\":\"esp32dev\",\"size\":10,\"sha256\":\"" + kSha +
      "\",\"sig\":\"" + kSig + "\",\"url\":\"https://yule.williamb.net/f.bin\",\"notes\":\"fixes\",\"extra\":{\"a\":[1]}}";
  const ManifestResult r = decodeManifest(json);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error.c_str());
  TEST_ASSERT_EQUAL_INT(2, r.manifest.v);
}

static void test_malformed_manifests_are_rejected_without_crashing() {
  for (const char* junk : {"", "garbage", "[]", "42", "null", "{", "{\"v\":1}", "{\"v\":1,\"version\":\"0.3.0\"}"}) {
    TEST_ASSERT_FALSE_MESSAGE(decodeManifest(junk).ok, junk);
  }
  TEST_ASSERT_FALSE(decodeManifest(std::string(5000, ' ') + manifestJson()).ok);
  // Bad field values.
  TEST_ASSERT_FALSE(decodeManifest(manifestJson("0.3")).ok);
  TEST_ASSERT_FALSE(decodeManifest(manifestJson("v0.3.0")).ok);
  TEST_ASSERT_FALSE(decodeManifest(manifestJson("0.3.0-beta")).ok);
  TEST_ASSERT_FALSE(decodeManifest(manifestJson("0.3.0", "ESP32 Dev")).ok);
  TEST_ASSERT_FALSE(decodeManifest(manifestJson("0.3.0", "")).ok);
  std::string json = manifestJson();
  json.replace(json.find(kSha), kSha.size(), std::string(63, 'a'));
  TEST_ASSERT_FALSE_MESSAGE(decodeManifest(json).ok, "short digest");
  json = manifestJson();
  json.replace(json.find(kSha), kSha.size(), std::string(64, 'g'));
  TEST_ASSERT_FALSE_MESSAGE(decodeManifest(json).ok, "non-hex digest");
  json = manifestJson();
  json.replace(json.find("\"size\":1534016"), 14, "\"size\":0");
  TEST_ASSERT_FALSE_MESSAGE(decodeManifest(json).ok, "zero size");
  json = manifestJson();
  json.replace(json.find("\"size\":1534016"), 14, "\"size\":-5");
  TEST_ASSERT_FALSE_MESSAGE(decodeManifest(json).ok, "negative size");
  json = manifestJson();
  json.replace(json.find(kSig), kSig.size(), "not base64!!!!!!");
  TEST_ASSERT_FALSE_MESSAGE(decodeManifest(json).ok, "bad signature");
}

static void test_digest_is_normalised_to_lower_case() {
  std::string json = manifestJson();
  json.replace(json.find(kSha), kSha.size(), std::string(64, 'A'));
  const ManifestResult r = decodeManifest(json);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING(kSha.c_str(), r.manifest.sha256.c_str());
}

static void test_semver_parsing_and_ordering() {
  SemVer a, b;
  TEST_ASSERT_TRUE(parseSemVer("0.3.0", a));
  TEST_ASSERT_EQUAL_INT(0, a.major);
  TEST_ASSERT_EQUAL_INT(3, a.minor);
  TEST_ASSERT_TRUE(parseSemVer("10.20.30", b));
  TEST_ASSERT_EQUAL_INT(30, b.patch);
  for (const char* bad : {"", "1", "1.2", "1.2.3.4", "a.b.c", "1.2.x", "1..3", "-1.2.3", "1.2.3-rc1", " 1.2.3", "1.2.3 ", "12345.0.0"}) {
    TEST_ASSERT_FALSE_MESSAGE(parseSemVer(bad, a), bad);
  }
  auto cmp = [](const char* x, const char* y) {
    SemVer p, q;
    TEST_ASSERT_TRUE(parseSemVer(x, p));
    TEST_ASSERT_TRUE(parseSemVer(y, q));
    return compareSemVer(p, q);
  };
  TEST_ASSERT_TRUE(cmp("0.3.0", "0.3.0") == 0);
  TEST_ASSERT_TRUE(cmp("0.3.1", "0.3.0") > 0);
  TEST_ASSERT_TRUE(cmp("0.2.9", "0.3.0") < 0);
  TEST_ASSERT_TRUE(cmp("0.10.0", "0.9.0") > 0);  // numeric, not alphabetical
  TEST_ASSERT_TRUE(cmp("1.0.0", "0.99.99") > 0);
  TEST_ASSERT_TRUE(cmp("0.0.9", "0.0.10") < 0);
}

// ---- 7.2 install decision ----

static void test_install_only_when_strictly_newer() {
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.3.1"), context("0.3.0")) == InstallDecision::Install);
  TEST_ASSERT_TRUE(decideInstall(manifestOf("1.0.0"), context("0.9.9")) == InstallDecision::Install);
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.3.0"), context("0.3.0")) == InstallDecision::SkipNotNewer);  // equal
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.2.0"), context("0.3.0")) == InstallDecision::SkipNotNewer);  // older
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.9.0"), context("0.10.0")) == InstallDecision::SkipNotNewer);
}

static void test_wrong_board_is_never_installed() {
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.9.0", "esp32-c3"), context("0.1.0")) == InstallDecision::SkipBoardMismatch);
  // Board mismatch is reported even when the version would also be skipped.
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.0.1", "esp32-s3"), context("0.1.0")) == InstallDecision::SkipBoardMismatch);
}

static void test_a_rolled_back_version_is_never_retried() {
  InstallContext c = context("0.2.0");
  c.rolledBackVersions = {"0.3.0"};
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.3.0"), c) == InstallDecision::SkipRolledBack);
  // A later fixed release then installs.
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.3.1"), c) == InstallDecision::Install);
  // An older, different version that was not rolled back from is still just "not newer" / allowed as appropriate.
  c.rolledBackVersions = {"0.3.0", "0.4.0"};
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.4.0"), c) == InstallDecision::SkipRolledBack);
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.5.0"), c) == InstallDecision::Install);
}

static void test_updates_wait_until_idle() {
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.3.1"), context("0.3.0", false)) == InstallDecision::SkipNotIdle);
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.3.1"), context("0.3.0", true)) == InstallDecision::Install);
  // Not idle never turns an unwanted release into a wanted one.
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.3.0"), context("0.3.0", false)) == InstallDecision::SkipNotNewer);
}

static void test_an_unparseable_running_version_installs_nothing() {
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.3.1"), context("0.0.0-dev")) == InstallDecision::SkipBadVersion);
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.3.1"), context("")) == InstallDecision::SkipBadVersion);
}

static void test_decision_names() {
  TEST_ASSERT_EQUAL_STRING("install", installDecisionName(InstallDecision::Install));
  TEST_ASSERT_EQUAL_STRING("not-newer", installDecisionName(InstallDecision::SkipNotNewer));
  TEST_ASSERT_EQUAL_STRING("rolled-back", installDecisionName(InstallDecision::SkipRolledBack));
  TEST_ASSERT_EQUAL_STRING("not-idle", installDecisionName(InstallDecision::SkipNotIdle));
  TEST_ASSERT_EQUAL_STRING("wrong-board", installDecisionName(InstallDecision::SkipBoardMismatch));
}

// ---- 7.3 rollback state machine ----

static void test_a_crash_looping_update_reverts_after_three_boots() {
  OtaState s;
  s.markInstalled();
  TEST_ASSERT_TRUE(s.provisional);
  TEST_ASSERT_TRUE(s.onBoot("0.3.0") == BootAction::Continue);  // boot 1
  TEST_ASSERT_EQUAL_UINT8(1, s.bootAttempts);
  TEST_ASSERT_TRUE(s.onBoot("0.3.0") == BootAction::Continue);  // boot 2
  TEST_ASSERT_TRUE(s.onBoot("0.3.0") == BootAction::Continue);  // boot 3
  TEST_ASSERT_TRUE(s.provisional);
  TEST_ASSERT_TRUE_MESSAGE(s.onBoot("0.3.0") == BootAction::Revert, "revert once three boots have failed");
  TEST_ASSERT_FALSE(s.provisional);
  TEST_ASSERT_TRUE(s.isRolledBack("0.3.0"));
  TEST_ASSERT_TRUE(s.lastResult.kind == OtaResult::Kind::RolledBack);
  TEST_ASSERT_EQUAL_STRING("rolled-back:0.3.0", s.lastResult.toString().c_str());
  // The previous firmware boots next: it is no longer provisional.
  TEST_ASSERT_TRUE(s.onBoot("0.2.0") == BootAction::Continue);
}

static void test_a_healthy_update_stops_counting_boots() {
  OtaState s;
  s.markInstalled();
  TEST_ASSERT_TRUE(s.onBoot("0.3.0") == BootAction::Continue);
  s.markHealthy();
  TEST_ASSERT_FALSE(s.provisional);
  TEST_ASSERT_EQUAL_UINT8(0, s.bootAttempts);
  TEST_ASSERT_TRUE(s.lastResult.kind == OtaResult::Kind::Ok);
  for (int i = 0; i < 10; ++i) TEST_ASSERT_TRUE(s.onBoot("0.3.0") == BootAction::Continue);
  TEST_ASSERT_FALSE(s.isRolledBack("0.3.0"));
}

static void test_boots_of_a_normal_image_are_not_counted() {
  OtaState s;
  for (int i = 0; i < 20; ++i) TEST_ASSERT_TRUE(s.onBoot("0.1.0") == BootAction::Continue);
  TEST_ASSERT_EQUAL_UINT8(0, s.bootAttempts);
}

static void test_health_when_wifi_and_broker_come_up_quickly() {
  HealthMonitor h;
  TEST_ASSERT_TRUE(h.update(1000) == HealthMonitor::Verdict::Pending);
  h.onWifi(true, 5000);
  TEST_ASSERT_TRUE(h.update(6000) == HealthMonitor::Verdict::Pending);
  h.onBrokerConnected();
  TEST_ASSERT_TRUE_MESSAGE(h.update(30000) == HealthMonitor::Verdict::Pending, "needs 60 s of uptime too");
  TEST_ASSERT_TRUE(h.update(59999) == HealthMonitor::Verdict::Pending);
  TEST_ASSERT_TRUE(h.update(60000) == HealthMonitor::Verdict::Healthy);
}

static void test_health_when_wifi_is_unavailable_never_reverts_on_the_broker() {
  HealthMonitor h;
  TEST_ASSERT_TRUE(h.update(59999) == HealthMonitor::Verdict::Pending);
  TEST_ASSERT_TRUE(h.update(60000) == HealthMonitor::Verdict::Healthy);  // no Wi-Fi: judged on uptime alone
  HealthMonitor late;
  TEST_ASSERT_TRUE(late.update(3600000) == HealthMonitor::Verdict::Healthy);
}

static void test_a_broker_that_never_connects_reverts_after_ten_minutes_of_wifi() {
  HealthMonitor h;
  h.onWifi(true, 20000);
  TEST_ASSERT_TRUE(h.update(60000) == HealthMonitor::Verdict::Pending);   // Wi-Fi up, no broker: not healthy yet
  TEST_ASSERT_TRUE(h.update(300000) == HealthMonitor::Verdict::Pending);
  TEST_ASSERT_TRUE(h.update(20000 + 600000 - 1) == HealthMonitor::Verdict::Pending);
  TEST_ASSERT_TRUE(h.update(20000 + 600000) == HealthMonitor::Verdict::Revert);
}

static void test_a_late_broker_connection_is_still_healthy() {
  HealthMonitor h;
  h.onWifi(true, 20000);
  TEST_ASSERT_TRUE(h.update(400000) == HealthMonitor::Verdict::Pending);
  h.onBrokerConnected();
  TEST_ASSERT_TRUE(h.update(400001) == HealthMonitor::Verdict::Healthy);
}

static void test_wifi_time_counts_only_while_wifi_is_up() {
  HealthMonitor h;
  h.onWifi(true, 10000);
  h.onWifi(false, 310000);  // 300 s of Wi-Fi, then it drops
  TEST_ASSERT_TRUE_MESSAGE(h.update(2000000) == HealthMonitor::Verdict::Pending, "a long Wi-Fi outage is not the new firmware's fault");
  h.onWifi(true, 2000000);
  TEST_ASSERT_TRUE(h.update(2000000 + 299999) == HealthMonitor::Verdict::Pending);
  TEST_ASSERT_TRUE(h.update(2000000 + 300000) == HealthMonitor::Verdict::Revert);  // 300 s + 300 s = 10 minutes
}

static void test_the_rolled_back_list_keeps_four() {
  OtaState s;
  for (const char* v : {"0.1.0", "0.2.0", "0.3.0", "0.4.0", "0.5.0"}) s.recordRollback(v);
  TEST_ASSERT_EQUAL_UINT32(4, s.rolledBack.size());
  TEST_ASSERT_FALSE(s.isRolledBack("0.1.0"));  // the oldest dropped out
  TEST_ASSERT_TRUE(s.isRolledBack("0.2.0"));
  TEST_ASSERT_TRUE(s.isRolledBack("0.5.0"));
  s.recordRollback("0.5.0");  // no duplicates
  TEST_ASSERT_EQUAL_UINT32(4, s.rolledBack.size());
}

static void test_failures_are_recorded_without_touching_provisional_state() {
  OtaState s;
  s.recordFailure("digest mismatch");
  TEST_ASSERT_TRUE(s.lastResult.kind == OtaResult::Kind::Failed);
  TEST_ASSERT_EQUAL_STRING("failed:digest mismatch", s.lastResult.toString().c_str());
  TEST_ASSERT_FALSE(s.provisional);
}

static void test_ota_state_round_trips_through_json() {
  OtaState s;
  s.markInstalled();
  s.onBoot("0.3.0");
  s.rolledBack = {"0.1.0", "0.2.0"};
  s.lastResult = OtaResult::fromString("failed:no space");
  const OtaState back = OtaState::fromJson(s.toJson());
  TEST_ASSERT_TRUE(back.provisional);
  TEST_ASSERT_EQUAL_UINT8(1, back.bootAttempts);
  TEST_ASSERT_EQUAL_UINT32(2, back.rolledBack.size());
  TEST_ASSERT_EQUAL_STRING("0.2.0", back.rolledBack[1].c_str());
  TEST_ASSERT_TRUE(back.lastResult == s.lastResult);
  // Damaged state loads as "nothing pending".
  for (const char* junk : {"", "garbage", "[]", "{\"prov\":\"yes\",\"att\":\"x\"}"}) {
    const OtaState d = OtaState::fromJson(junk);
    TEST_ASSERT_FALSE(d.provisional);
    TEST_ASSERT_EQUAL_UINT8(0, d.bootAttempts);
  }
}

static void test_result_strings() {
  TEST_ASSERT_EQUAL_STRING("none", OtaResult().toString().c_str());
  TEST_ASSERT_EQUAL_STRING("ok", OtaResult::fromString("ok").toString().c_str());
  TEST_ASSERT_EQUAL_STRING("rolled-back:0.3.0", OtaResult::fromString("rolled-back:0.3.0").toString().c_str());
  TEST_ASSERT_EQUAL_STRING("failed:x:y", OtaResult::fromString("failed:x:y").toString().c_str());
  TEST_ASSERT_TRUE(OtaResult::fromString("whatever").kind == OtaResult::Kind::None);
}

static void test_a_full_bad_release_story() {
  // 0.3.0 crashes at boot and rolls back; the same release is not retried; 0.3.1 later installs.
  OtaState s;
  InstallContext ctx = context("0.2.0");
  const OtaManifest bad = manifestOf("0.3.0");
  TEST_ASSERT_TRUE(decideInstall(bad, ctx) == InstallDecision::Install);
  s.markInstalled();
  for (int i = 0; i < 3; ++i) TEST_ASSERT_TRUE(s.onBoot("0.3.0") == BootAction::Continue);
  TEST_ASSERT_TRUE(s.onBoot("0.3.0") == BootAction::Revert);
  ctx.rolledBackVersions = s.rolledBack;
  TEST_ASSERT_TRUE(decideInstall(bad, ctx) == InstallDecision::SkipRolledBack);
  TEST_ASSERT_TRUE(decideInstall(manifestOf("0.3.1"), ctx) == InstallDecision::Install);
}

void run_ota_tests() {
  RUN_TEST(test_a_valid_manifest_decodes);
  RUN_TEST(test_each_missing_field_is_rejected);
  RUN_TEST(test_non_https_urls_are_rejected);
  RUN_TEST(test_unknown_fields_are_ignored);
  RUN_TEST(test_malformed_manifests_are_rejected_without_crashing);
  RUN_TEST(test_digest_is_normalised_to_lower_case);
  RUN_TEST(test_semver_parsing_and_ordering);
  RUN_TEST(test_install_only_when_strictly_newer);
  RUN_TEST(test_wrong_board_is_never_installed);
  RUN_TEST(test_a_rolled_back_version_is_never_retried);
  RUN_TEST(test_updates_wait_until_idle);
  RUN_TEST(test_an_unparseable_running_version_installs_nothing);
  RUN_TEST(test_decision_names);
  RUN_TEST(test_a_crash_looping_update_reverts_after_three_boots);
  RUN_TEST(test_a_healthy_update_stops_counting_boots);
  RUN_TEST(test_boots_of_a_normal_image_are_not_counted);
  RUN_TEST(test_health_when_wifi_and_broker_come_up_quickly);
  RUN_TEST(test_health_when_wifi_is_unavailable_never_reverts_on_the_broker);
  RUN_TEST(test_a_broker_that_never_connects_reverts_after_ten_minutes_of_wifi);
  RUN_TEST(test_a_late_broker_connection_is_still_healthy);
  RUN_TEST(test_wifi_time_counts_only_while_wifi_is_up);
  RUN_TEST(test_the_rolled_back_list_keeps_four);
  RUN_TEST(test_failures_are_recorded_without_touching_provisional_state);
  RUN_TEST(test_ota_state_round_trips_through_json);
  RUN_TEST(test_result_strings);
  RUN_TEST(test_a_full_bad_release_story);
}
