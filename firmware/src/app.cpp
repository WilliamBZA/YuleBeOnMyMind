#include "app.h"

#include <ESPmDNS.h>
#include <esp_mac.h>

#include "cues.h"
#include "device_id.h"
#include "log.h"
#include "status_payload.h"
#include "text_util.h"

namespace {

const char* gestureName(yule::GestureEvent e) {
  switch (e) {
    case yule::GestureEvent::ShortPress: return "short press";
    case yule::GestureEvent::LongPress: return "long press";
    case yule::GestureEvent::LongThresholdReached: return "held 2 s";
    case yule::GestureEvent::VeryLongThresholdReached: return "held 10 s";
    case yule::GestureEvent::Released: return "released";
    case yule::GestureEvent::None: break;
  }
  return "none";
}

// "Brander Grandparents, Smith Grandparents" or "none".
std::string groupNames(const yule::Membership& m) {
  if (m.groups.empty()) return "none";
  std::string out;
  for (const yule::Group& g : m.groups) {
    if (!out.empty()) out += ", ";
    out += g.name;
  }
  return out;
}

}  // namespace

void App::begin() {
  Serial.begin(115200);
  delay(300);  // let a serial monitor attach so the first lines are not lost

  uint8_t mac[6] = {};
  esp_efuse_mac_get_default(mac);  // the factory MAC: the same across re-flashing and Wi-Fi resets
  deviceId_ = yule::deviceIdFromMacBytes(mac);
  suffix_ = yule::deviceSuffix(deviceId_);
  hostname_ = "yule-" + suffix_;

  yuleLog("boot", "Yule Be On My Mind %s (%s)", YULE_FW_VERSION, YULE_BOARD_ID);
  yuleLog("boot", "device id %s", deviceId_.c_str());

  profile_ = storage_.loadProfile();
  ident_ = storage_.loadIdent();
  membership_ = storage_.loadMembership();
  otaState_ = storage_.loadOtaState();
  yuleLog("boot", "owner '%s'; groups: %s; channel %s", profile_.displayName.c_str(),
          groupNames(membership_).c_str(), membership_.channel.c_str());
  if (!ident_.hasPassword()) {
    yuleLog("boot", "no broker password yet: setup required (type 'password <value>' here, or use the settings page)");
  }

  leds_.begin();
  leds_.sweep();
  compositor_.setBrightness(profile_.brightness);
  button_.begin();

  time_.begin(profile_.timeZone.rule);
  configureBroker();
  wifi_.begin(hostname_, "YuleBeOnMyMind-" + suffix_, millis());
  yuleLog("boot", "ready; type 'help' for serial commands");
}

void App::loop() {
  const uint32_t now = millis();
  tickButton(now);
  tickWifi(now);
  tickClock();
  tickBroker(now);
  tickConsole();
  tickRender(now);
  delay(1);  // yield to the Wi-Fi and MQTT tasks
}

// ---- Button ------------------------------------------------------------------------------------------

void App::tickButton(uint32_t nowMs) {
  button_.update(gestures_, nowMs);
  yule::GestureEvent event;
  while (gestures_.poll(event)) {
    yuleLog("button", "%s", gestureName(event));
    if (event == yule::GestureEvent::VeryLongThresholdReached) {
      yuleLog("button", "very long press: opening Wi-Fi setup");
      wifi_.openPortalOnDemand(nowMs);
    }
  }
}

// ---- Wi-Fi, mDNS and time -----------------------------------------------------------------------------

void App::tickWifi(uint32_t nowMs) {
  wifi_.update(nowMs);
  const bool up = wifi_.connected();
  if (up == wifiWasUp_) return;
  wifiWasUp_ = up;
  if (up) {
    startMdns();
    time_.onWifiUp();  // (re)start SNTP
  } else {
    MDNS.end();
  }
}

void App::startMdns() {
  MDNS.end();  // a fresh start announces the new address after a reconnect
  if (MDNS.begin(hostname_.c_str())) {
    yuleLog("wifi", "reachable as %s.local (%s)", hostname_.c_str(), wifi_.localIp().c_str());
  } else {
    yuleLog("wifi", "mDNS could not start; use the IP address %s", wifi_.localIp().c_str());
  }
}

void App::tickClock() {
  const bool synced = time_.synced();
  if (synced && !clockWasSynced_) {
    yuleLog("time", "clock synchronised: %s (zone rule %s)", time_.localTimeString().c_str(), time_.rule().c_str());
  }
  clockWasSynced_ = synced;
}

// ---- Broker ------------------------------------------------------------------------------------------

void App::configureBroker() {
  MqttSettings settings;
  settings.host = ident_.brokerHost.empty() ? YULE_BROKER_HOST : ident_.brokerHost;
  settings.port = ident_.brokerPort != 0 ? ident_.brokerPort : static_cast<uint16_t>(YULE_BROKER_PORT);
  settings.username = deviceId_;
  settings.clientId = deviceId_;
  settings.password = ident_.password;
  settings.willTopic = topics_.status(deviceId_);
  settings.willPayload = yule::encodeStatusOffline();
  mqtt_.configure(settings);
}

void App::tickBroker(uint32_t nowMs) {
  yule::BrokerGateInputs in;
  in.hasPassword = ident_.hasPassword();
  in.wifiUp = wifi_.connected();
  in.wifiUpForMs = wifi_.connectedForMs(nowMs);
  in.clockSynced = time_.synced();
  in.clockWaitMs = YULE_NTP_WAIT_MS;
  const yule::BrokerGate gate = yule::evaluateBrokerGate(in);
  if (!gateLogged_ || gate != lastGate_) {
    yuleLog("mqtt", "broker connection: %s", yule::brokerGateName(gate));
    lastGate_ = gate;
    gateLogged_ = true;
  }

  mqtt_.update(nowMs, gate == yule::BrokerGate::Open);

  MqttEvent event;
  while (mqtt_.poll(event)) onMqttEvent(event);
}

void App::onMqttEvent(const MqttEvent& event) {
  switch (event.type) {
    case MqttEvent::Type::Connected:
      onConnected(event.sessionPresent);
      break;
    case MqttEvent::Type::Message:
      onMessage(event);
      break;
    case MqttEvent::Type::Disconnected:
    case MqttEvent::Type::Published:
      break;  // the link logs connection changes; acknowledgements are used by sending (14.1)
  }
}

void App::onConnected(bool sessionPresent) {
  (void)sessionPresent;
  subscribeToEverything();
  publishOnlineStatus();
}

void App::subscribeToEverything() {
  // Subscribing again to a topic the broker already has for our session is harmless, so the whole list is
  // used after every connect. Groups removed while we were offline are dropped when the retained config
  // arrives (see onConfigMessage).
  for (const std::string& topic : yule::desiredSubscriptions(topics_, deviceId_, YULE_BOARD_ID, membership_)) {
    mqtt_.subscribe(topic);
    yuleLog("member", "subscribe %s", topic.c_str());
  }
}

void App::publishOnlineStatus() {
  yule::StatusInfo info;
  info.firmwareVersion = YULE_FW_VERSION;
  info.board = YULE_BOARD_ID;
  info.displayName = profile_.displayName;
  info.otaResult = otaState_.lastResult.toString();
  if (mqtt_.publish(topics_.status(deviceId_), yule::encodeStatusOnline(info), 1, true) < 0) {
    yuleLog("mqtt", "could not publish the online status");
  } else {
    yuleLog("mqtt", "announced online");
  }
}

void App::onMessage(const MqttEvent& event) {
  if (event.topic == topics_.config(deviceId_)) {
    onConfigMessage(event.payload);
    return;
  }

  yule::Topics::PressTopic press;
  if (topics_.parsePress(event.topic, press)) {
    // Playback is wired up with the receiving task (14.2); until then a press is only reported.
    yuleLog("mqtt", "press from %s via group %s (%u bytes)", press.sender.c_str(), press.group.c_str(),
            static_cast<unsigned>(event.payload.size()));
    return;
  }

  if (event.topic == topics_.manifest(membership_.channel, YULE_BOARD_ID)) {
    // Installing updates is wired up with the OTA task (16.3).
    yuleLog("mqtt", "firmware manifest received on %s (%u bytes)", event.topic.c_str(),
            static_cast<unsigned>(event.payload.size()));
    return;
  }

  yuleLog("mqtt", "ignored message on unexpected topic %s", event.topic.c_str());
}

// ---- Membership --------------------------------------------------------------------------------------

void App::onConfigMessage(const std::string& payload) {
  const yule::ConfigResult result = yule::decodeDeviceConfig(payload);
  if (!result.ok) {
    yuleLog("member", "config rejected (%s); keeping the current groups", result.error.c_str());
    return;
  }
  if (result.membership == membership_) {
    yuleLog("member", "config received: no change");
    return;
  }

  const yule::SubscriptionChange change =
      yule::planMembershipChange(topics_, YULE_BOARD_ID, membership_, result.membership);
  storage_.saveMembership(result.membership);  // remembered across reboots, usable before the broker answers
  membership_ = result.membership;
  yuleLog("member", "now in %u group(s): %s; channel %s", static_cast<unsigned>(membership_.groups.size()),
          groupNames(membership_).c_str(), membership_.channel.c_str());
  applySubscriptionChange(change);
}

void App::applySubscriptionChange(const yule::SubscriptionChange& change) {
  for (const std::string& topic : change.unsubscribe) {
    mqtt_.unsubscribe(topic);
    yuleLog("member", "unsubscribe %s", topic.c_str());
  }
  for (const std::string& topic : change.subscribe) {
    mqtt_.subscribe(topic);
    yuleLog("member", "subscribe %s", topic.c_str());
  }
}

// ---- LEDs --------------------------------------------------------------------------------------------

void App::tickRender(uint32_t nowMs) {
  // Base layer: the Wi-Fi setup indication while the portal is open, otherwise nothing (status indications
  // for connecting and a lost connection come with 14.4).
  const bool setup = wifi_.portalActive();
  if (setup != setupShown_) {
    compositor_.setBase(setup ? yule::makeSetupIndication() : std::shared_ptr<yule::Animation>(), nowMs);
    setupShown_ = setup;
  }

  if (nowMs - lastFrameMs_ >= YULE_FRAME_INTERVAL_MS) {
    lastFrameMs_ = nowMs;
    compositor_.render(nowMs, frame_, YULE_LED_COUNT);
    leds_.show(frame_, YULE_LED_COUNT);
  }
}

// ---- Serial commands ---------------------------------------------------------------------------------

void App::tickConsole() {
  std::string line;
  if (console_.poll(line)) onConsoleLine(line);
}

void App::onConsoleLine(const std::string& line) {
  const std::string text = yule::trimWhitespace(line);
  const size_t space = text.find_first_of(" \t");
  const std::string command = text.substr(0, space);
  const std::string argument = space == std::string::npos ? std::string() : yule::trimWhitespace(text.substr(space));

  if (command == "password") {
    if (argument.empty()) {
      yuleLog("setup", "usage: password <the password the server showed for this device>");
      return;
    }
    ident_.password = argument;
    storage_.saveIdent(ident_);
    configureBroker();
    mqtt_.reconnect();
    // The password itself is never echoed.
    yuleLog("setup", "broker password saved (%u characters); connecting", static_cast<unsigned>(argument.size()));
  } else if (command == "status") {
    printStatus();
  } else if (command == "help") {
    yuleLog("setup", "commands: password <value> | status | help");
  } else {
    yuleLog("setup", "unknown command '%s'; type 'help'", command.c_str());
  }
}

void App::printStatus() {
  yuleLog("status", "device %s, firmware %s (%s), uptime %lu s", deviceId_.c_str(), YULE_FW_VERSION, YULE_BOARD_ID,
          millis() / 1000UL);
  if (wifi_.portalActive()) {
    yuleLog("status", "Wi-Fi: setup portal open (YuleBeOnMyMind-%s)", suffix_.c_str());
  } else if (wifi_.connected()) {
    yuleLog("status", "Wi-Fi: connected to '%s', IP %s, %s.local", wifi_.ssid().c_str(), wifi_.localIp().c_str(),
            hostname_.c_str());
  } else {
    yuleLog("status", "Wi-Fi: not connected");
  }
  yuleLog("status", "clock: %s", time_.synced() ? time_.localTimeString().c_str() : "not synchronised");
  yuleLog("status", "broker: %s; password %s", mqtt_.stateName(), ident_.hasPassword() ? "set" : "NOT SET");
  yuleLog("status", "groups: %s; channel %s", groupNames(membership_).c_str(), membership_.channel.c_str());
}
