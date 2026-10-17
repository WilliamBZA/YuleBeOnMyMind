// Tests for the Wi-Fi supervisor (13.1), the broker gate and clock check (13.3, 13.4) and the subscription
// plan (13.5).
#include <unity.h>

#include <algorithm>
#include <string>
#include <vector>

#include "connection_policy.h"
#include "subscription_plan.h"
#include "wifi_supervisor.h"

using namespace yule;

namespace {

using State = WifiSupervisor::State;
using Reason = WifiSupervisor::PortalReason;

// Drives a supervisor with a scripted network: `connected` and `saved` are what the glue would report.
class Rig {
 public:
  explicit Rig(uint32_t startMs = 0, WifiSupervisor::Config cfg = WifiSupervisor::Config())
      : sup(cfg), now(startMs) {}

  WifiSupervisor::Actions boot(bool hasSaved) {
    saved = hasSaved;
    return record(sup.start(now, saved));
  }

  // Advances time in `stepMs` steps, calling update each step. Returns the actions of the last step.
  void run(uint32_t durationMs, uint32_t stepMs = 100) {
    const uint32_t end = now + durationMs;
    while (static_cast<int32_t>(end - now) > 0) {
      now += stepMs;
      record(sup.update(now, connected, saved));
    }
  }

  WifiSupervisor::Actions record(const WifiSupervisor::Actions& a) {
    if (a.beginConnect) beginTimes.push_back(now);
    if (a.openPortal) ++portalOpens;
    if (a.closePortal) ++portalCloses;
    return a;
  }

  WifiSupervisor sup;
  uint32_t now;
  bool connected = false;
  bool saved = false;
  std::vector<uint32_t> beginTimes;
  int portalOpens = 0;
  int portalCloses = 0;
};

constexpr uint32_t kMinute = 60000;

}  // namespace

// ---- First boot and setup ----

void test_a_fresh_device_opens_the_setup_portal() {
  Rig r;
  const WifiSupervisor::Actions a = r.boot(false);
  TEST_ASSERT_TRUE(a.openPortal);
  TEST_ASSERT_FALSE(a.beginConnect);
  TEST_ASSERT_TRUE(r.sup.portalActive());
  TEST_ASSERT_EQUAL(static_cast<int>(Reason::FirstRun), static_cast<int>(r.sup.portalReason()));
}

void test_a_first_run_portal_never_times_out() {
  Rig r;
  r.boot(false);
  r.run(3 * 60 * kMinute);
  TEST_ASSERT_TRUE(r.sup.portalActive());
  TEST_ASSERT_EQUAL(1, r.portalOpens);
  TEST_ASSERT_EQUAL(0, r.portalCloses);
}

void test_a_wrong_password_leaves_the_portal_open() {
  Rig r;
  r.boot(false);
  r.run(30000);  // the user tried a network, it was rejected: still not connected
  TEST_ASSERT_TRUE(r.sup.portalActive());
  TEST_ASSERT_EQUAL(1, r.portalOpens);
}

void test_completing_setup_closes_the_portal_and_connects() {
  Rig r;
  r.boot(false);
  r.run(45000);
  r.connected = true;
  r.saved = true;
  r.run(200);
  TEST_ASSERT_TRUE(r.sup.connected());
  TEST_ASSERT_EQUAL(1, r.portalCloses);
  TEST_ASSERT_EQUAL(0, static_cast<int>(r.beginTimes.size()));  // the portal itself did the connecting
}

void test_a_connection_right_after_opening_the_portal_is_ignored() {
  // The old connection can still look "connected" for a moment after the glue disconnects it.
  Rig r(1000);
  r.boot(true);
  r.connected = true;
  r.run(500);
  TEST_ASSERT_TRUE(r.sup.connected());
  r.record(r.sup.requestPortal(r.now));
  r.run(500);  // still reporting connected
  TEST_ASSERT_TRUE(r.sup.portalActive());
  r.connected = false;
  r.run(2000);
  TEST_ASSERT_TRUE(r.sup.portalActive());
  TEST_ASSERT_EQUAL(0, r.portalCloses);
}

// ---- Saved network ----

void test_a_saved_network_is_tried_without_opening_the_portal() {
  Rig r;
  const WifiSupervisor::Actions a = r.boot(true);
  TEST_ASSERT_TRUE(a.beginConnect);
  TEST_ASSERT_FALSE(a.openPortal);
  r.connected = true;
  r.run(3000);
  TEST_ASSERT_TRUE(r.sup.connected());
  TEST_ASSERT_EQUAL(0, r.portalOpens);
}

void test_a_router_that_is_down_for_ten_minutes_never_opens_the_portal() {
  Rig r;
  r.boot(true);
  r.run(10 * kMinute);  // never connects
  TEST_ASSERT_EQUAL(0, r.portalOpens);
  TEST_ASSERT_FALSE(r.sup.portalActive());
  TEST_ASSERT_TRUE(r.beginTimes.size() >= 5);
  // The router comes back: the next attempt joins it without anyone touching the device.
  r.connected = true;
  r.run(2 * kMinute);
  TEST_ASSERT_TRUE(r.sup.connected());
  TEST_ASSERT_EQUAL(0, r.portalOpens);
}

void test_retries_back_off_and_never_wait_more_than_a_minute() {
  Rig r;
  r.boot(true);
  r.run(30 * kMinute, 50);
  const std::vector<uint32_t>& t = r.beginTimes;
  TEST_ASSERT_TRUE(t.size() > 10);
  uint32_t longestGap = 0;
  for (size_t i = 1; i < t.size(); ++i) longestGap = std::max(longestGap, t[i] - t[i - 1]);
  // gap = attempt timeout (20 s) + back-off wait (at most 60 s)
  TEST_ASSERT_TRUE(longestGap <= 20000 + 60000 + 100);
  // Early gaps are shorter than late ones: the waits grow.
  TEST_ASSERT_TRUE((t[1] - t[0]) < (t[t.size() - 1] - t[t.size() - 2]));
}

void test_the_back_off_wait_is_capped_at_sixty_seconds() {
  Rig r;
  r.boot(true);
  uint32_t maxWait = 0;
  for (int i = 0; i < 2000; ++i) {  // about 33 minutes of an outage
    r.run(1000, 50);
    if (r.sup.state() == State::BackingOff) maxWait = std::max(maxWait, r.sup.currentBackoffMs());
  }
  TEST_ASSERT_TRUE(maxWait >= 45000);  // the cap (60 s) is reached, less up to 25% jitter
  TEST_ASSERT_TRUE(maxWait <= 60000);
}

void test_losing_the_network_retries_and_a_success_resets_the_back_off() {
  Rig r;
  r.boot(true);
  r.connected = true;
  r.run(2000);
  TEST_ASSERT_TRUE(r.sup.connected());
  r.connected = false;  // Wi-Fi drops
  r.run(100);
  TEST_ASSERT_EQUAL(static_cast<int>(State::BackingOff), static_cast<int>(r.sup.state()));
  const size_t before = r.beginTimes.size();
  r.run(3000);  // first wait is about a second
  TEST_ASSERT_TRUE(r.beginTimes.size() > before);
  // A long outage grows the wait; one success resets it.
  r.run(10 * kMinute);
  r.connected = true;
  r.run(120000);
  TEST_ASSERT_TRUE(r.sup.connected());
  r.connected = false;
  r.run(100);
  TEST_ASSERT_TRUE(r.sup.currentBackoffMs() <= 1000);
}

void test_the_station_reconnecting_by_itself_is_noticed_while_backing_off() {
  Rig r;
  r.boot(true);
  r.run(20100);  // the first attempt has just timed out
  TEST_ASSERT_EQUAL(static_cast<int>(State::BackingOff), static_cast<int>(r.sup.state()));
  r.connected = true;
  r.run(200);
  TEST_ASSERT_TRUE(r.sup.connected());
}

void test_losing_the_saved_network_while_waiting_opens_the_portal() {
  Rig r;
  r.boot(true);
  r.run(25000);
  r.saved = false;
  r.run(120000);
  TEST_ASSERT_TRUE(r.sup.portalActive());
  TEST_ASSERT_EQUAL(static_cast<int>(Reason::FirstRun), static_cast<int>(r.sup.portalReason()));
}

// ---- Very long press ----

void test_a_very_long_press_opens_the_portal_from_any_state() {
  for (int variant = 0; variant < 3; ++variant) {
    Rig r;
    r.boot(true);
    if (variant == 0) {
      r.connected = true;
      r.run(2000);
    } else if (variant == 1) {
      r.run(1000);  // attempting
    } else {
      r.run(25000);  // backing off
    }
    const WifiSupervisor::Actions a = r.sup.requestPortal(r.now);
    TEST_ASSERT_TRUE(a.openPortal);
    TEST_ASSERT_TRUE(r.sup.portalActive());
    TEST_ASSERT_EQUAL(static_cast<int>(Reason::OnDemand), static_cast<int>(r.sup.portalReason()));
  }
}

void test_a_new_network_entered_in_the_on_demand_portal_connects() {
  Rig r;
  r.boot(true);
  r.connected = true;
  r.run(2000);
  r.connected = false;  // the glue disconnects when it opens the portal
  r.record(r.sup.requestPortal(r.now));
  r.run(60000);
  TEST_ASSERT_TRUE(r.sup.portalActive());
  r.connected = true;  // the owner finished setup
  r.run(300);
  TEST_ASSERT_TRUE(r.sup.connected());
  TEST_ASSERT_EQUAL(1, r.portalCloses);
  TEST_ASSERT_EQUAL(1, r.portalOpens);
}

void test_an_unused_on_demand_portal_closes_after_ten_minutes_and_retries_the_old_network() {
  Rig r;
  r.boot(true);
  r.connected = true;
  r.run(2000);
  r.connected = false;
  r.record(r.sup.requestPortal(r.now));
  const size_t beginsBefore = r.beginTimes.size();

  r.run(10 * kMinute - 1000);
  TEST_ASSERT_TRUE(r.sup.portalActive());
  TEST_ASSERT_EQUAL(0, r.portalCloses);

  r.run(1000);  // 10 minutes since it opened
  TEST_ASSERT_FALSE(r.sup.portalActive());
  TEST_ASSERT_EQUAL(1, r.portalCloses);
  TEST_ASSERT_EQUAL(static_cast<int>(beginsBefore + 1), static_cast<int>(r.beginTimes.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(State::Attempting), static_cast<int>(r.sup.state()));

  r.connected = true;  // the old network is still there
  r.run(1000);
  TEST_ASSERT_TRUE(r.sup.connected());
}

void test_an_on_demand_portal_with_nothing_to_go_back_to_stays_open() {
  Rig r;
  r.boot(true);
  r.record(r.sup.requestPortal(r.now));
  r.saved = false;
  r.run(15 * kMinute);
  TEST_ASSERT_TRUE(r.sup.portalActive());
  TEST_ASSERT_EQUAL(0, r.portalCloses);
  TEST_ASSERT_EQUAL(static_cast<int>(Reason::FirstRun), static_cast<int>(r.sup.portalReason()));
}

void test_asking_again_while_the_portal_is_open_does_not_extend_it() {
  Rig r;
  r.boot(true);
  r.record(r.sup.requestPortal(r.now));
  r.run(9 * kMinute);
  const WifiSupervisor::Actions again = r.sup.requestPortal(r.now);
  TEST_ASSERT_FALSE(again.any());
  r.run(kMinute + 200);
  TEST_ASSERT_FALSE(r.sup.portalActive());
  TEST_ASSERT_EQUAL(1, r.portalOpens);
}

void test_timing_is_correct_across_the_millisecond_clock_wrap() {
  Rig r(0xFFFFFFFFu - 4 * kMinute);  // the 32-bit counter wraps four minutes in
  r.boot(true);
  r.record(r.sup.requestPortal(r.now));
  r.run(10 * kMinute - 1000);
  TEST_ASSERT_TRUE(r.sup.portalActive());
  r.run(1000);
  TEST_ASSERT_FALSE(r.sup.portalActive());
  TEST_ASSERT_EQUAL(1, r.portalCloses);
}

void test_back_off_retries_work_across_the_millisecond_clock_wrap() {
  Rig r(0xFFFFFFFFu - 30000);
  r.boot(true);
  r.run(10 * kMinute, 50);
  TEST_ASSERT_TRUE(r.beginTimes.size() >= 5);
  TEST_ASSERT_EQUAL(0, r.portalOpens);
}

// ---- Broker gate and clock ----

void test_the_clock_check() {
  TEST_ASSERT_FALSE(isClockSynced(0));
  TEST_ASSERT_FALSE(isClockSynced(86400));  // 1970
  TEST_ASSERT_FALSE(isClockSynced(kMinSyncedEpochSeconds - 1));
  TEST_ASSERT_TRUE(isClockSynced(kMinSyncedEpochSeconds));
  TEST_ASSERT_TRUE(isClockSynced(1797000000));  // late 2026
}

void test_a_device_without_a_password_never_connects() {
  BrokerGateInputs in;
  in.hasPassword = false;
  in.wifiUp = true;
  in.wifiUpForMs = 10 * kMinute;
  in.clockSynced = true;
  TEST_ASSERT_EQUAL(static_cast<int>(BrokerGate::NoPassword), static_cast<int>(evaluateBrokerGate(in)));
  in.wifiUp = false;
  TEST_ASSERT_EQUAL(static_cast<int>(BrokerGate::NoPassword), static_cast<int>(evaluateBrokerGate(in)));
}

void test_the_broker_needs_wifi() {
  BrokerGateInputs in;
  in.hasPassword = true;
  in.wifiUp = false;
  in.clockSynced = true;
  TEST_ASSERT_EQUAL(static_cast<int>(BrokerGate::NoWifi), static_cast<int>(evaluateBrokerGate(in)));
}

void test_the_connection_waits_for_the_clock_for_fifteen_seconds() {
  BrokerGateInputs in;
  in.hasPassword = true;
  in.wifiUp = true;
  in.clockSynced = false;
  in.wifiUpForMs = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(BrokerGate::WaitingForClock), static_cast<int>(evaluateBrokerGate(in)));
  in.wifiUpForMs = 14999;
  TEST_ASSERT_EQUAL(static_cast<int>(BrokerGate::WaitingForClock), static_cast<int>(evaluateBrokerGate(in)));
  in.wifiUpForMs = 15000;
  TEST_ASSERT_EQUAL(static_cast<int>(BrokerGate::Open), static_cast<int>(evaluateBrokerGate(in)));
}

void test_a_synchronised_clock_opens_the_gate_at_once() {
  BrokerGateInputs in;
  in.hasPassword = true;
  in.wifiUp = true;
  in.clockSynced = true;
  in.wifiUpForMs = 10;
  TEST_ASSERT_EQUAL(static_cast<int>(BrokerGate::Open), static_cast<int>(evaluateBrokerGate(in)));
}

void test_the_clock_wait_is_configurable() {
  BrokerGateInputs in;
  in.hasPassword = true;
  in.wifiUp = true;
  in.clockSynced = false;
  in.clockWaitMs = 3000;
  in.wifiUpForMs = 2999;
  TEST_ASSERT_EQUAL(static_cast<int>(BrokerGate::WaitingForClock), static_cast<int>(evaluateBrokerGate(in)));
  in.wifiUpForMs = 3000;
  TEST_ASSERT_EQUAL(static_cast<int>(BrokerGate::Open), static_cast<int>(evaluateBrokerGate(in)));
}

// ---- Subscription plan ----

namespace {
Membership members(std::initializer_list<const char*> ids, const char* channel = "stable") {
  Membership m;
  m.channel = channel;
  for (const char* id : ids) m.groups.push_back({id, std::string("name of ") + id});
  return m;
}
}  // namespace

void test_the_full_subscription_list_for_two_groups() {
  const Topics topics("yule/v1");
  const std::vector<std::string> want = desiredSubscriptions(
      topics, "d-3c71bf4a2b10", "esp32dev", members({"g-7f3k2abc", "g-q7m4x2td"}));
  TEST_ASSERT_EQUAL(4, static_cast<int>(want.size()));
  TEST_ASSERT_EQUAL_STRING("yule/v1/devices/d-3c71bf4a2b10/config", want[0].c_str());
  TEST_ASSERT_EQUAL_STRING("yule/v1/firmware/stable/esp32dev", want[1].c_str());
  TEST_ASSERT_EQUAL_STRING("yule/v1/groups/g-7f3k2abc/press/+", want[2].c_str());
  TEST_ASSERT_EQUAL_STRING("yule/v1/groups/g-q7m4x2td/press/+", want[3].c_str());
}

void test_a_device_in_no_groups_still_listens_for_config_and_firmware() {
  const Topics topics("yule/v1");
  const std::vector<std::string> want =
      desiredSubscriptions(topics, "d-3c71bf4a2b10", "esp32-c3", members({}, "beta"));
  TEST_ASSERT_EQUAL(2, static_cast<int>(want.size()));
  TEST_ASSERT_EQUAL_STRING("yule/v1/devices/d-3c71bf4a2b10/config", want[0].c_str());
  TEST_ASSERT_EQUAL_STRING("yule/v1/firmware/beta/esp32-c3", want[1].c_str());
}

void test_adding_a_group_subscribes_to_only_that_group() {
  const Topics topics("yule/v1");
  const SubscriptionChange c =
      planMembershipChange(topics, "esp32dev", members({"g-7f3k2abc"}), members({"g-7f3k2abc", "g-q7m4x2td"}));
  TEST_ASSERT_EQUAL(1, static_cast<int>(c.subscribe.size()));
  TEST_ASSERT_EQUAL_STRING("yule/v1/groups/g-q7m4x2td/press/+", c.subscribe[0].c_str());
  TEST_ASSERT_TRUE(c.unsubscribe.empty());
}

void test_removing_a_group_unsubscribes_from_only_that_group() {
  const Topics topics("yule/v1");
  const SubscriptionChange c =
      planMembershipChange(topics, "esp32dev", members({"g-7f3k2abc", "g-q7m4x2td"}), members({"g-q7m4x2td"}));
  TEST_ASSERT_TRUE(c.subscribe.empty());
  TEST_ASSERT_EQUAL(1, static_cast<int>(c.unsubscribe.size()));
  TEST_ASSERT_EQUAL_STRING("yule/v1/groups/g-7f3k2abc/press/+", c.unsubscribe[0].c_str());
}

void test_a_channel_change_swaps_the_manifest_topic() {
  const Topics topics("yule/v1");
  const SubscriptionChange c = planMembershipChange(topics, "esp32dev", members({"g-7f3k2abc"}, "stable"),
                                                    members({"g-7f3k2abc"}, "beta"));
  TEST_ASSERT_EQUAL(1, static_cast<int>(c.subscribe.size()));
  TEST_ASSERT_EQUAL_STRING("yule/v1/firmware/beta/esp32dev", c.subscribe[0].c_str());
  TEST_ASSERT_EQUAL(1, static_cast<int>(c.unsubscribe.size()));
  TEST_ASSERT_EQUAL_STRING("yule/v1/firmware/stable/esp32dev", c.unsubscribe[0].c_str());
}

void test_a_rename_changes_no_subscriptions() {
  const Topics topics("yule/v1");
  Membership before = members({"g-7f3k2abc"});
  Membership after = before;
  after.groups[0].name = "Renamed";
  TEST_ASSERT_TRUE(planMembershipChange(topics, "esp32dev", before, after).empty());
}

void test_an_empty_group_list_unsubscribes_from_everything() {
  const Topics topics("yule/v1");
  const SubscriptionChange c =
      planMembershipChange(topics, "esp32dev", members({"g-7f3k2abc", "g-q7m4x2td"}), members({}));
  TEST_ASSERT_TRUE(c.subscribe.empty());
  TEST_ASSERT_EQUAL(2, static_cast<int>(c.unsubscribe.size()));
}

void test_the_same_membership_changes_nothing() {
  const Topics topics("yule/v1");
  TEST_ASSERT_TRUE(
      planMembershipChange(topics, "esp32dev", members({"g-7f3k2abc"}), members({"g-7f3k2abc"})).empty());
}

void run_network_tests() {
  RUN_TEST(test_a_fresh_device_opens_the_setup_portal);
  RUN_TEST(test_a_first_run_portal_never_times_out);
  RUN_TEST(test_a_wrong_password_leaves_the_portal_open);
  RUN_TEST(test_completing_setup_closes_the_portal_and_connects);
  RUN_TEST(test_a_connection_right_after_opening_the_portal_is_ignored);
  RUN_TEST(test_a_saved_network_is_tried_without_opening_the_portal);
  RUN_TEST(test_a_router_that_is_down_for_ten_minutes_never_opens_the_portal);
  RUN_TEST(test_retries_back_off_and_never_wait_more_than_a_minute);
  RUN_TEST(test_the_back_off_wait_is_capped_at_sixty_seconds);
  RUN_TEST(test_losing_the_network_retries_and_a_success_resets_the_back_off);
  RUN_TEST(test_the_station_reconnecting_by_itself_is_noticed_while_backing_off);
  RUN_TEST(test_losing_the_saved_network_while_waiting_opens_the_portal);
  RUN_TEST(test_a_very_long_press_opens_the_portal_from_any_state);
  RUN_TEST(test_a_new_network_entered_in_the_on_demand_portal_connects);
  RUN_TEST(test_an_unused_on_demand_portal_closes_after_ten_minutes_and_retries_the_old_network);
  RUN_TEST(test_an_on_demand_portal_with_nothing_to_go_back_to_stays_open);
  RUN_TEST(test_asking_again_while_the_portal_is_open_does_not_extend_it);
  RUN_TEST(test_timing_is_correct_across_the_millisecond_clock_wrap);
  RUN_TEST(test_back_off_retries_work_across_the_millisecond_clock_wrap);
  RUN_TEST(test_the_clock_check);
  RUN_TEST(test_a_device_without_a_password_never_connects);
  RUN_TEST(test_the_broker_needs_wifi);
  RUN_TEST(test_the_connection_waits_for_the_clock_for_fifteen_seconds);
  RUN_TEST(test_a_synchronised_clock_opens_the_gate_at_once);
  RUN_TEST(test_the_clock_wait_is_configurable);
  RUN_TEST(test_the_full_subscription_list_for_two_groups);
  RUN_TEST(test_a_device_in_no_groups_still_listens_for_config_and_firmware);
  RUN_TEST(test_adding_a_group_subscribes_to_only_that_group);
  RUN_TEST(test_removing_a_group_unsubscribes_from_only_that_group);
  RUN_TEST(test_a_channel_change_swaps_the_manifest_topic);
  RUN_TEST(test_a_rename_changes_no_subscriptions);
  RUN_TEST(test_an_empty_group_list_unsubscribes_from_everything);
  RUN_TEST(test_the_same_membership_changes_nothing);
}
