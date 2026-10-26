#include "app.h"

#include <ESPmDNS.h>
#include <esp_mac.h>
#include <esp_random.h>

#include "device_id.h"
#include "effects.h"
#include "log.h"
#include "press_message.h"
#include "quiet_hours.h"
#include "rtttl.h"
#include "settings_validation.h"
#include "song_library.h"
#include "status_payload.h"
#include "text_util.h"

namespace {

constexpr uint32_t kOtaCheckEveryMs = 1000;
constexpr uint32_t kOtaRetryAfterFailureMs = 30UL * 60UL * 1000UL;  // a failed install is tried again later

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

yule::Melody parseOrSilence(const char* rtttl) {
  const yule::RtttlResult r = yule::parseRtttl(rtttl);
  return r.ok ? r.melody : yule::Melody();
}

bool reached(uint32_t nowMs, uint32_t deadlineMs) { return static_cast<int32_t>(nowMs - deadlineMs) >= 0; }

}  // namespace

App::App() : wifi_(storage_), topics_(YULE_TOPIC_ROOT), web_(*this), controls_(YULE_COOLDOWN_MS) {}

void App::begin() {
  Serial.begin(115200);
  delay(300);  // let a serial monitor attach so the first lines are not lost

  uint8_t mac[6] = {};
  esp_efuse_mac_get_default(mac);  // the factory MAC: the same across re-flashing and Wi-Fi resets
  deviceId_ = yule::deviceIdFromMacBytes(mac);
  suffix_ = yule::deviceSuffix(deviceId_);
  hostname_ = "yule-" + suffix_;
  receiver_.reset(new yule::PressReceiver(topics_, deviceId_));

  yuleLog("boot", "Yule Be On My Mind %s (%s)%s", YULE_FW_VERSION, YULE_BOARD_ID,
          YULE_BRINGUP ? " - bring-up build, no networking" : "");
  yuleLog("boot", "device id %s", deviceId_.c_str());

  profile_ = storage_.loadProfile();
  ident_ = storage_.loadIdent();
  membership_ = storage_.loadMembership();
  otaState_ = storage_.loadOtaState();
  checkFirmwareOnBoot();

  yuleLog("boot", "owner '%s'; groups: %s; channel %s; %s", profile_.displayName.c_str(), groupNames(membership_).c_str(),
          membership_.channel.c_str(), profile_.muted ? "muted" : "sound on");

  toneSent_ = parseOrSilence(yule::kToneSent);
  toneFailed_ = parseOrSilence(yule::kToneSendFailed);
  toneUnmuted_ = parseOrSilence(yule::kToneMutedOff);
  chime_ = parseOrSilence(yule::kDefaultChime);
  progress_ = std::make_shared<yule::UpdateProgressIndication>();

  leds_.begin();
  leds_.sweep();
  compositor_.setBrightness(profile_.brightness);
  buzzer_.begin();
  button_.begin();

#if YULE_BRINGUP
  yuleLog("boot", "bring-up: a short press plays your own song and pattern here; a long press toggles mute");
#else
  if (!ident_.hasPassword()) {
    yuleLog("boot", "no broker password yet: setup required (type 'password <value>' here, or use the settings page)");
  }
  releaseKeysUsable_ = OtaClient::usableReleaseKeys() > 0;
  if (!releaseKeysUsable_) {
    yuleLog("ota", "no usable release public key in secrets.h: firmware updates cannot be installed");
  }
  time_.begin(profile_.timeZone.rule);
  configureBroker();
  wifi_.begin(hostname_, "YuleBeOnMyMind-" + suffix_, millis());
#endif
  yuleLog("boot", "ready; type 'help' for serial commands");
}

void App::checkFirmwareOnBoot() {
  const bool wasProvisional = otaState_.provisional;
  const std::string installed = otaState_.installedVersion;
  const yule::BootAction action = otaState_.onBoot(YULE_FW_VERSION);
  storage_.saveOtaState(otaState_);  // the boot count must be safe before anything else can crash
  if (action == yule::BootAction::Revert) {
    revertFirmware("it restarted 3 times without becoming healthy");
  } else if (wasProvisional && !otaState_.provisional) {
    yuleLog("ota", "the update to %s never started properly; back on %s, and %s will not be installed again",
            installed.c_str(), YULE_FW_VERSION, installed.c_str());
  }
  // Confirm the image to the bootloader now that our own boot count is saved (see otaConfirmRunningImage).
  otaConfirmRunningImage();
  if (otaState_.provisional) {
    yuleLog("ota", "running freshly installed firmware %s: checking it (boot %u of %u)", YULE_FW_VERSION,
            static_cast<unsigned>(otaState_.bootAttempts), static_cast<unsigned>(yule::limits::kMaxProvisionalBoots));
  }
}

void App::loop() {
  const uint32_t now = millis();
  tickButton(now);
#if !YULE_BRINGUP
  tickWifi(now);
#endif
  tickClock(now);
#if !YULE_BRINGUP
  tickBroker(now);
#endif
  tickSend(now);
  tickPlayback(now);
  tickSound(now);
  tickIndication(now);
  tickRender(now);
#if !YULE_BRINGUP
  tickWeb();
  tickOta(now);
  tickHealth(now);
#endif
  tickConsole();
  delay(1);  // yield to the Wi-Fi and MQTT tasks
}

// ---- Button ------------------------------------------------------------------------------------------

void App::tickButton(uint32_t nowMs) {
  button_.update(gestures_, nowMs);
  yule::GestureEvent event;
  while (gestures_.poll(event)) {
    yuleLog("button", "%s", gestureName(event));
    onControl(controls_.onGesture(event, nowMs), nowMs);
  }
}

void App::onControl(yule::ControlAction action, uint32_t nowMs) {
  switch (action) {
    case yule::ControlAction::SendPress:
#if YULE_BRINGUP
      playOwnPatternLocally(nowMs);
      controls_.onSendResult(false, nowMs);  // nothing was sent, so no cooldown
#else
      sendPress(nowMs);
#endif
      break;
    case yule::ControlAction::PleaseWait:
      if (controls_.sendInFlight()) {
        yuleLog("send", "please wait: the last press is still being sent");
      } else {
        yuleLog("send", "please wait: cooldown, %lu s left",
                static_cast<unsigned long>((controls_.cooldown().remainingMs(nowMs) + 999) / 1000));
      }
      showCue(yule::Cue::PleaseWait, nowMs);
      break;
    case yule::ControlAction::ToggleMute:
      toggleMute(nowMs);
      break;
    case yule::ControlAction::ShowLongCue:
      showCue(yule::Cue::LongPressReached, nowMs);
      break;
    case yule::ControlAction::OpenWifiSetup:
      showCue(yule::Cue::VeryLongPressReached, nowMs);
#if YULE_BRINGUP
      yuleLog("button", "very long press: there is no Wi-Fi setup in the bring-up build");
#else
      yuleLog("button", "very long press: opening Wi-Fi setup");
      web_.stop();  // WiFiManager serves port 80 while its portal is open
      wifi_.openPortalOnDemand(nowMs);
#endif
      break;
    case yule::ControlAction::ClearHoldCue:
      if (holdCueShown_) {
        compositor_.clearOverlay();
        holdCueShown_ = false;
      }
      break;
    case yule::ControlAction::None:
      break;
  }
}

// ---- Sending -----------------------------------------------------------------------------------------

std::string App::newMessageId() {
  char id[17];
  snprintf(id, sizeof(id), "%08lx%08lx", static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
  return id;
}

void App::sendPress(uint32_t nowMs) {
  sendPublished_ = false;
  if (!mqtt_.connected()) {
    yuleLog("send", "not sent: not connected to the broker");
    tracker_.failNow();  // fails at once: nothing is kept to send later
    return;
  }
  if (membership_.groups.empty()) {
    yuleLog("send", "not sent: this device is not in any family group yet");
    tracker_.failNow();
    return;
  }

  yule::PressMessage m;
  m.id = newMessageId();
  m.from = deviceId_;
  m.name = profile_.displayName;
  m.ts = time_.epochSeconds();  // 0 (left out) while the clock is not synchronised
  m.song = yule::resolveSongRtttl(profile_.song);
  m.pattern = profile_.pattern;
  const std::string payload = yule::encodePress(m);

  // One copy per group, all with the same id; each must be acknowledged by the broker (QoS 1).
  std::vector<int> copies;
  for (const yule::Group& g : membership_.groups) {
    const int msgId = mqtt_.publish(topics_.press(g.id, deviceId_), payload, 1, false);
    if (msgId < 0) {
      yuleLog("send", "press %s: could not hand the copy for '%s' to the broker link", m.id.c_str(), g.name.c_str());
      tracker_.failNow();
      return;
    }
    copies.push_back(msgId);
  }
  tracker_.begin(nowMs, copies);
  sendPublished_ = true;
  yuleLog("send", "press %s published to %u group(s), %u bytes; waiting for the broker", m.id.c_str(),
          static_cast<unsigned>(copies.size()), static_cast<unsigned>(payload.size()));
}

void App::tickSend(uint32_t nowMs) {
  tracker_.update(nowMs);
  bool success = false;
  if (tracker_.takeResult(success)) finishSend(success, nowMs);
}

void App::finishSend(bool success, uint32_t nowMs) {
  controls_.onSendResult(success, nowMs);
  if (success) {
    yuleLog("send", "sent: the broker accepted the press for every group; cooldown %lu s",
            static_cast<unsigned long>(controls_.cooldown().durationMs() / 1000));
    showCue(yule::Cue::Sent, nowMs);
    playTone(toneSent_, nowMs);
  } else {
    if (sendPublished_) yuleLog("send", "send failed: not every group was acknowledged within 5 s");
    showCue(yule::Cue::SendFailed, nowMs);
    playTone(toneFailed_, nowMs);
  }
  sendPublished_ = false;
}

// ---- Mute and feedback -------------------------------------------------------------------------------

void App::toggleMute(uint32_t nowMs) {
  profile_.muted = !profile_.muted;
  storage_.saveProfile(profile_);
  yuleLog("sound", "%s", profile_.muted ? "muted: lights only" : "unmuted");
  showCue(profile_.muted ? yule::Cue::Muted : yule::Cue::Unmuted, nowMs);
  if (!profile_.muted) playTone(toneUnmuted_, nowMs);
}

void App::showCue(yule::Cue cue, uint32_t nowMs) {
  compositor_.setOverlay(yule::makeCue(cue), nowMs);
  holdCueShown_ = cue == yule::Cue::LongPressReached || cue == yule::Cue::VeryLongPressReached;
}

void App::playTone(const yule::Melody& tone, uint32_t nowMs) { tonePlayer_.start(tone, nowMs); }

yule::SoundContext App::soundContext() const {
  yule::SoundContext ctx;
  ctx.muted = profile_.muted;
  ctx.quiet = profile_.quiet;
  ctx.clockSynced = clockWasSynced_;
  ctx.minuteOfDay = minuteOfDay_;
  return ctx;
}

void App::playOwnPatternLocally(uint32_t nowMs) {
  yule::PressMessage m;
  m.id = newMessageId();
  m.from = deviceId_;
  m.name = profile_.displayName;
  m.song = yule::resolveSongRtttl(profile_.song);
  m.pattern = profile_.pattern;
  scheduler_.playNow(makeItem(m, false), nowMs);  // not a preview, so mute applies and can be tried out
}

// ---- Playback, sound and LEDs ------------------------------------------------------------------------

yule::PlaybackItem App::makeItem(const yule::PressMessage& message, bool preview) const {
  yule::PlaybackItem item;
  item.message = message;
  item.preview = preview;
  const yule::RtttlResult song = yule::parseRtttl(message.song);
  item.melody = song.ok ? song.melody : chime_;  // decodePress already swapped bad songs for the chime
  return item;
}

void App::tickPlayback(uint32_t nowMs) {
  const yule::PlaybackScheduler::Events ev = scheduler_.update(nowMs);
  // In the order they happen when the loop was late: the old item fades and ends before a new one starts.
  if (ev.fadeStarted) {
    compositor_.startFadeOut(nowMs);
    songPlayer_.stop();
  }
  if (ev.ended) {
    songPlayer_.stop();
    songIsPreview_ = false;
    playbackOwnsBase_ = false;
    indicationShown_ = false;  // the status indication (or darkness) comes back
  }
  if (ev.started) {
    const yule::PlaybackItem* item = scheduler_.current();
    if (item) {
      compositor_.setBase(std::shared_ptr<yule::Animation>(yule::makeEffect(item->message.pattern, esp_random())), nowMs);
      songPlayer_.start(item->melody, nowMs);
      songIsPreview_ = item->preview;
      playbackOwnsBase_ = true;
      yuleLog("play", "%s '%s': %s, song %.1f s, lights %.1f s%s", item->preview ? "preview for" : "playing",
              item->message.name.c_str(), yule::effectName(item->message.pattern.effect), songPlayer_.clippedMs() / 1000.0,
              yule::PlaybackScheduler::lightsDurationMs(songPlayer_.clippedMs()) / 1000.0,
              songPlayer_.totalMs() > songPlayer_.clippedMs() ? " (song cut off at 30 s)" : "");
    }
  }
}

void App::tickSound(uint32_t nowMs) {
  const uint16_t songHz = songPlayer_.update(nowMs);
  const uint16_t toneHz = tonePlayer_.update(nowMs);
  // Re-evaluated every tick, so muting or the start of quiet hours silences the buzzer at once.
  yule::SoundContext ctx = soundContext();
  const bool tonesAllowed = yule::soundAllowed(ctx);
  ctx.preview = songIsPreview_;
  const bool songAllowed = yule::soundAllowed(ctx);
  uint16_t hz = 0;
  if (tonePlayer_.playing() && tonesAllowed) {
    hz = toneHz;  // a feedback tone briefly takes over; the song's timeline carries on underneath
  } else if (songPlayer_.playing() && songAllowed) {
    hz = songHz;
  }
  buzzer_.play(hz);
}

void App::tickIndication(uint32_t nowMs) {
#if YULE_BRINGUP
  const yule::Indication wanted = yule::Indication::None;
#else
  yule::IndicationInputs in;
  in.updating = ota_.busy();
  in.portalOpen = wifi_.portalActive();
  in.brokerConnected = mqtt_.connected();
  in.everConnected = everConnected_;
  in.uptimeMs = nowMs;
  in.disconnectedForMs = mqtt_.connected() ? 0 : nowMs - disconnectedSinceMs_;
  in.connectingForMs = YULE_CONNECTING_INDICATION_MS;
  in.lostAfterMs = YULE_CONNECTION_LOST_AFTER_MS;
  const yule::Indication wanted = yule::chooseIndication(in);
  if (wanted == yule::Indication::Updating) progress_->setPercent(ota_.percent());
#endif

  // Playback, and the pause between two presses, own the base layer; only the update indication outranks it.
  if (wanted != yule::Indication::Updating && (playbackOwnsBase_ || scheduler_.phase() != yule::PlaybackScheduler::Phase::Idle)) {
    return;
  }
  if (indicationShown_ && wanted == indication_) return;
  if (wanted != indication_) yuleLog("status", "indication: %s", yule::indicationName(wanted));
  indication_ = wanted;
  indicationShown_ = true;
  playbackOwnsBase_ = false;

  std::shared_ptr<yule::Animation> base;
  switch (wanted) {
    case yule::Indication::Setup: base = yule::makeSetupIndication(); break;
    case yule::Indication::Connecting: base = yule::makeConnectingIndication(); break;
    case yule::Indication::ConnectionLost: base = yule::makeConnectionLostIndication(); break;
    case yule::Indication::Updating: base = progress_; break;
    case yule::Indication::None: break;  // all LEDs off
  }
  compositor_.setBase(base, nowMs);
}

void App::tickRender(uint32_t nowMs) {
  if (whiteTest_ && reached(nowMs, whiteTestUntilMs_)) {
    whiteTest_ = false;
    compositor_.clearOverlay();
    compositor_.setBrightness(profile_.brightness);
    yuleLog("leds", "white test over; brightness back to %u%%", static_cast<unsigned>(profile_.brightness));
  }
  if (nowMs - lastFrameMs_ < YULE_FRAME_INTERVAL_MS) return;
  lastFrameMs_ = nowMs;
  compositor_.render(nowMs, frame_, YULE_LED_COUNT);
  leds_.show(frame_, YULE_LED_COUNT);
}

// ---- Wi-Fi, mDNS, time and the settings page ---------------------------------------------------------

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
    MDNS.addService("http", "tcp", 80);
    yuleLog("wifi", "reachable as %s.local (%s)", hostname_.c_str(), wifi_.localIp().c_str());
  } else {
    yuleLog("wifi", "mDNS could not start; use the IP address %s", wifi_.localIp().c_str());
  }
}

void App::tickClock(uint32_t nowMs) {
  if (clockCheckedMs_ != 0 && nowMs - clockCheckedMs_ < 1000) return;  // local time is needed to the minute
  clockCheckedMs_ = nowMs == 0 ? 1 : nowMs;
  const bool synced = time_.synced();
  if (synced && !clockWasSynced_) {
    yuleLog("time", "clock synchronised: %s (zone rule %s)", time_.localTimeString().c_str(), time_.rule().c_str());
  }
  clockWasSynced_ = synced;
  if (synced) minuteOfDay_ = time_.localMinuteOfDay();
}

void App::tickWeb() {
  const bool wanted = wifi_.connected() && !wifi_.portalActive();
  if (wanted && !web_.running()) {
    web_.begin(hostname_);
  } else if (!wanted && web_.running()) {
    web_.stop();
  }
  web_.handle();
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
  gate_ = yule::evaluateBrokerGate(in);
  if (!gateLogged_ || gate_ != lastGate_) {
    yuleLog("mqtt", "broker connection: %s", yule::brokerGateName(gate_));
    lastGate_ = gate_;
    gateLogged_ = true;
  }

  // While an update installs, the connection is closed cleanly. The broker keeps this device's session and
  // holds presses for it, which are delivered after the restart (or after a failed attempt).
  const bool pause = ota_.busy();
  if (pause != otaPausedBroker_) {
    otaPausedBroker_ = pause;
    yuleLog("mqtt", "%s", pause ? "pausing the broker connection while the update installs; presses wait at the broker"
                                : "resuming the broker connection");
  }

  mqtt_.update(nowMs, gate_ == yule::BrokerGate::Open && !pause);

  MqttEvent event;
  while (mqtt_.poll(event)) onMqttEvent(event, nowMs);
}

void App::onMqttEvent(const MqttEvent& event, uint32_t nowMs) {
  switch (event.type) {
    case MqttEvent::Type::Connected:
      onConnected(event.sessionPresent, nowMs);
      break;
    case MqttEvent::Type::Disconnected:
      disconnectedSinceMs_ = nowMs;
      break;
    case MqttEvent::Type::Message:
      onMessage(event, nowMs);
      break;
    case MqttEvent::Type::Published:
      tracker_.onAck(event.msgId, nowMs);
      break;
  }
}

void App::onConnected(bool sessionPresent, uint32_t nowMs) {
  (void)sessionPresent;
  (void)nowMs;
  everConnected_ = true;
  health_.onBrokerConnected();
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
    yuleLog("mqtt", "announced online (update result: %s)", info.otaResult.c_str());
  }
}

void App::onMessage(const MqttEvent& event, uint32_t nowMs) {
  if (event.topic == topics_.config(deviceId_)) {
    onConfigMessage(event.payload);
  } else if (event.topic == topics_.manifest(membership_.channel, YULE_BOARD_ID)) {
    onManifest(event.payload);
  } else {
    onPress(event, nowMs);  // the receive filters say why anything unexpected is dropped
  }
}

void App::onPress(const MqttEvent& event, uint32_t nowMs) {
  const yule::PressReceiver::Result r = receiver_->receive(event.topic, event.payload, membership_, time_.epochSeconds());
  if (!r.accepted()) {
    yuleLog("recv", "dropped (%s): %u bytes on %s", yule::dropReasonName(r.drop), static_cast<unsigned>(event.payload.size()),
            event.topic.c_str());
    return;
  }
  const bool busy = !scheduler_.idle();
  if (!scheduler_.enqueue(makeItem(r.message, false), nowMs)) {
    yuleLog("recv", "dropped (queue-full): press %s from '%s'", r.message.id.c_str(), r.message.name.c_str());
    return;
  }
  yuleLog("recv", "press %s from '%s' (%s) via group %s: %s", r.message.id.c_str(), r.message.name.c_str(),
          r.message.from.c_str(), r.group.c_str(), busy ? "queued" : "playing now");
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

// ---- Firmware updates --------------------------------------------------------------------------------

void App::onManifest(const std::string& payload) {
  if (payload.empty()) {
    hasManifest_ = false;
    yuleLog("ota", "no release is published for %s on %s", YULE_BOARD_ID, membership_.channel.c_str());
    return;
  }
  const yule::ManifestResult r = yule::decodeManifest(payload);
  if (!r.ok) {
    yuleLog("ota", "release manifest rejected: %s", r.error.c_str());
    return;
  }
  const bool same = hasManifest_ && r.manifest.version == manifest_.version && r.manifest.sha256 == manifest_.sha256;
  manifest_ = r.manifest;
  hasManifest_ = true;
  if (!same) {
    decisionLogged_ = false;
    otaRetryAtMs_ = millis();
    yuleLog("ota", "release %s is published for %s on %s", manifest_.version.c_str(), YULE_BOARD_ID,
            membership_.channel.c_str());
  }
}

bool App::idleForUpdate() const {
  // Not while anything plays or waits to play, a cue or tone shows, Wi-Fi setup runs, the button is held,
  // or a press is still being sent.
  return scheduler_.idle() && !compositor_.hasOverlay() && !tonePlayer_.playing() && !wifi_.portalActive() &&
         !gestures_.held() && !tracker_.pending() && !controls_.sendInFlight();
}

void App::tickOta(uint32_t nowMs) {
  switch (ota_.phase()) {
    case OtaClient::Phase::ReadyToRestart:
      // Provisional from the first boot on: ota_policy now counts its boots and watches its health.
      otaState_.markInstalled(ota_.version());
      storage_.saveOtaState(otaState_);
      yuleLog("ota", "firmware %s verified and installed; restarting into it", ota_.version().c_str());
      buzzer_.silence();
      delay(300);  // let the log line out
      ESP.restart();
      return;
    case OtaClient::Phase::Failed: {
      std::string reason;
      if (ota_.takeFailure(reason)) {
        otaState_.recordFailure(reason);
        storage_.saveOtaState(otaState_);
        otaRetryAtMs_ = nowMs + kOtaRetryAfterFailureMs;
        decisionLogged_ = false;
        yuleLog("ota", "update to %s failed: %s; still running %s, trying again in 30 minutes", ota_.version().c_str(),
                reason.c_str(), YULE_FW_VERSION);
        // The failure reaches the server's Status page with the online status when the broker link resumes.
      }
      return;
    }
    case OtaClient::Phase::Downloading:
    case OtaClient::Phase::Verifying: {
      const int quarter = ota_.percent() / 25 * 25;
      if (quarter != otaLoggedPercent_) {
        otaLoggedPercent_ = quarter;
        yuleLog("ota", "downloading %s: %d%%", ota_.version().c_str(), quarter);
      }
      return;
    }
    case OtaClient::Phase::Idle:
      break;
  }

  if (!hasManifest_ || nowMs - otaCheckedMs_ < kOtaCheckEveryMs || !reached(nowMs, otaRetryAtMs_)) return;
  otaCheckedMs_ = nowMs;

  yule::InstallContext ctx;
  ctx.board = YULE_BOARD_ID;
  ctx.runningVersion = YULE_FW_VERSION;
  ctx.rolledBackVersions = otaState_.rolledBack;
  ctx.idle = idleForUpdate();
  const yule::InstallDecision decision = yule::decideInstall(manifest_, ctx);
  if (!decisionLogged_ || decision != lastDecision_) {
    if (decision != yule::InstallDecision::Install) {
      yuleLog("ota", "release %s: not installing now (%s)", manifest_.version.c_str(), yule::installDecisionName(decision));
    }
    decisionLogged_ = true;
  }
  lastDecision_ = decision;
  if (decision != yule::InstallDecision::Install) return;

  if (!releaseKeysUsable_) {
    otaState_.recordFailure("this firmware has no usable release public key");
    storage_.saveOtaState(otaState_);
    otaRetryAtMs_ = nowMs + kOtaRetryAfterFailureMs;
    yuleLog("ota", "cannot install %s: this firmware has no usable release public key", manifest_.version.c_str());
    return;
  }
  otaLoggedPercent_ = -1;
  if (ota_.start(manifest_)) {
    yuleLog("ota", "installing %s (%lu bytes) from %s", manifest_.version.c_str(), static_cast<unsigned long>(manifest_.size),
            manifest_.url.c_str());
  }
}

void App::tickHealth(uint32_t nowMs) {
  if (!otaState_.provisional) return;
  const bool wifiUp = wifi_.connected();
  if (wifiUp != healthWifiUp_) {
    health_.onWifi(wifiUp, nowMs);
    healthWifiUp_ = wifiUp;
  }
  switch (health_.update(nowMs)) {
    case yule::HealthMonitor::Verdict::Pending:
      return;
    case yule::HealthMonitor::Verdict::Healthy:
      otaState_.markHealthy();
      storage_.saveOtaState(otaState_);
      yuleLog("ota", "firmware %s is healthy: the update is complete", YULE_FW_VERSION);
      if (mqtt_.connected()) publishOnlineStatus();
      return;
    case yule::HealthMonitor::Verdict::Revert:
      otaState_.recordRollback(YULE_FW_VERSION);
      storage_.saveOtaState(otaState_);
      revertFirmware("it has had Wi-Fi for 10 minutes without reaching the broker");
      return;
  }
}

void App::revertFirmware(const char* why) {
  yuleLog("ota", "rolling back firmware %s: %s", YULE_FW_VERSION, why);
  std::string error;
  otaRevertToPreviousFirmware(error);  // restarts on success
  yuleLog("ota", "cannot roll back (%s); staying on %s", error.c_str(), YULE_FW_VERSION);
}

yule::UpdateView App::updateView() const {
  yule::UpdateView v;
  switch (ota_.phase()) {
    case OtaClient::Phase::Downloading:
      v.state = yule::UpdateView::State::Downloading;
      v.percent = ota_.percent();
      v.version = ota_.version();
      return v;
    case OtaClient::Phase::Verifying:
      v.state = yule::UpdateView::State::Verifying;
      v.version = ota_.version();
      return v;
    case OtaClient::Phase::ReadyToRestart:
      v.state = yule::UpdateView::State::Restarting;
      v.version = ota_.version();
      return v;
    case OtaClient::Phase::Failed:
    case OtaClient::Phase::Idle:
      break;
  }
  if (otaState_.provisional) {
    v.state = yule::UpdateView::State::Checking;
    v.version = YULE_FW_VERSION;
    return v;
  }
  if (hasManifest_ && lastDecision_ == yule::InstallDecision::SkipNotIdle) {
    v.state = yule::UpdateView::State::Waiting;
    v.version = manifest_.version;
    return v;
  }
  switch (otaState_.lastResult.kind) {
    case yule::OtaResult::Kind::Ok:
      v.state = yule::UpdateView::State::Installed;
      v.version = YULE_FW_VERSION;
      break;
    case yule::OtaResult::Kind::Failed:
      v.state = yule::UpdateView::State::Failed;
      v.detail = otaState_.lastResult.detail;
      break;
    case yule::OtaResult::Kind::RolledBack:
      v.state = yule::UpdateView::State::RolledBack;
      v.version = otaState_.lastResult.detail;
      break;
    case yule::OtaResult::Kind::None:
      v.state = yule::UpdateView::State::UpToDate;
      break;
  }
  return v;
}

// ---- Settings page -----------------------------------------------------------------------------------

std::string App::statusJson() {
  yule::StatusView s;
  s.deviceId = deviceId_;
  s.firmwareVersion = YULE_FW_VERSION;
  s.board = YULE_BOARD_ID;
  s.groupNames = membership_.groupNames();
  s.channel = membership_.channel;
  if (wifi_.connected()) {
    s.wifiSsid = wifi_.ssid().c_str();
    s.ip = wifi_.localIp().c_str();
  }
  s.hostname = hostname_ + ".local";
  s.brokerConnected = mqtt_.connected();
  if (s.brokerConnected) {
    s.broker = "connected";
  } else if (otaPausedBroker_) {
    s.broker = "paused while the update installs";
  } else if (gate_ == yule::BrokerGate::NoPassword) {
    s.broker = "setup required";
  } else if (gate_ != yule::BrokerGate::Open) {
    s.broker = yule::brokerGateName(gate_);
  } else {
    s.broker = mqtt_.stateName();  // "connecting", or "idle" while waiting to retry
    if (s.broker == "idle") s.broker = "not connected; retrying";
  }
  s.brokerHost = ident_.brokerHost.empty() ? YULE_BROKER_HOST : ident_.brokerHost;
  s.brokerPort = ident_.brokerPort != 0 ? ident_.brokerPort : static_cast<uint16_t>(YULE_BROKER_PORT);
  s.brokerOverrideHost = ident_.brokerHost;
  s.brokerOverridePort = ident_.brokerPort;
  s.setupRequired = !ident_.hasPassword();
  s.timeSynced = time_.synced();
  s.localTime = time_.localTimeString();
  s.muted = profile_.muted;
  const yule::SoundContext ctx = soundContext();
  s.quietNow = yule::inQuietHours(ctx.quiet, ctx.clockSynced, ctx.minuteOfDay);
  s.uptimeS = millis() / 1000;
  s.update = updateView();
  return yule::encodeStatus(s);
}

std::string App::settingsJson() { return yule::encodeProfile(profile_); }

std::string App::saveSettings(const std::string& body, bool& ok) {
  const yule::SettingsUpdate u = yule::applySettingsJson(profile_, body);
  ok = u.applied();
  if (!ok) {
    yuleLog("web", "settings not saved: %u problem(s), first: %s", static_cast<unsigned>(u.validation.errors.size()),
            u.validation.errors.empty() ? "?" : u.validation.errors.front().message.c_str());
    return yule::encodeValidation(u.validation);
  }
  const bool zoneChanged = u.profile.timeZone.rule != profile_.timeZone.rule;
  const bool nameChanged = u.profile.displayName != profile_.displayName;
  profile_ = u.profile;
  storage_.saveProfile(profile_);  // the next press uses it; nothing is sent to anyone now
  compositor_.setBrightness(profile_.brightness);
  if (zoneChanged) {
    time_.setTimeZone(profile_.timeZone.rule);
    clockCheckedMs_ = 0;  // recompute the local time for quiet hours straight away
  }
  if (nameChanged && mqtt_.connected()) publishOnlineStatus();  // the server's Status page shows the name
  yuleLog("web", "settings saved: '%s', %s, %s, brightness %u%%, %s, quiet hours %s, zone %s", profile_.displayName.c_str(),
          profile_.song.kind == yule::SongChoice::Kind::Custom ? "custom song" : profile_.song.libraryId.c_str(),
          yule::effectName(profile_.pattern.effect), static_cast<unsigned>(profile_.brightness),
          profile_.muted ? "muted" : "sound on",
          profile_.quiet.enabled ? (yule::formatClockTime(profile_.quiet.startMinute) + "-" +
                                    yule::formatClockTime(profile_.quiet.endMinute)).c_str()
                                 : "off",
          profile_.timeZone.name.c_str());
  return yule::encodeValidation(u.validation);
}

std::string App::preview(const std::string& body, bool& ok) {
  yule::SettingsUpdate u = yule::applySettingsJson(profile_, body);
  if (u.applied() && ota_.busy()) u.validation.error("", "A firmware update is installing. Try the preview again in a minute.");
  ok = u.validation.ok();
  if (!ok) return yule::encodeValidation(u.validation);

  // The selection as it is on the page, saved or not. It plays here only and is never broadcast.
  yule::PressMessage m;
  m.id = "preview";
  m.from = deviceId_;
  m.name = u.profile.displayName;
  m.song = yule::resolveSongRtttl(u.profile.song);
  m.pattern = u.profile.pattern;
  scheduler_.playNow(makeItem(m, true), millis());
  yuleLog("web", "preview: %s with %s (plays here only, with sound even when muted)",
          u.profile.song.kind == yule::SongChoice::Kind::Custom ? "custom song" : u.profile.song.libraryId.c_str(),
          yule::effectName(u.profile.pattern.effect));
  return yule::encodeValidation(u.validation);
}

std::string App::saveDevice(const std::string& body, bool& ok) {
  const yule::DeviceUpdate u = yule::applyDeviceJson(ident_, body);
  ok = u.applied();
  if (!ok) return yule::encodeValidation(u.validation);
  const bool passwordChanged = u.ident.password != ident_.password;
  ident_ = u.ident;
  storage_.saveIdent(ident_);
  configureBroker();
  mqtt_.reconnect();
  // The password itself is never logged or returned.
  yuleLog("setup", "device setup saved (password %s, broker %s:%u); reconnecting", passwordChanged ? "changed" : "unchanged",
          ident_.brokerHost.empty() ? YULE_BROKER_HOST : ident_.brokerHost.c_str(),
          static_cast<unsigned>(ident_.brokerPort != 0 ? ident_.brokerPort : YULE_BROKER_PORT));
  return yule::encodeValidation(u.validation);
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
    yule::ValidationResult v;
    if (!yule::validatePassword(argument, v)) {
      yuleLog("setup", "%s", v.errors.front().message.c_str());
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
  } else if (command == "white") {
    // Every LED at full white and 100% brightness, to check the strip's current draw on a USB power meter.
    yule::Pattern white;
    white.effect = yule::Effect::Solid;
    white.colors[0] = yule::Rgb(255, 255, 255);
    white.colorCount = 1;
    compositor_.setBrightness(100);
    compositor_.setOverlay(std::shared_ptr<yule::Animation>(yule::makeEffect(white)), millis());
    holdCueShown_ = false;
    whiteTest_ = true;
    whiteTestUntilMs_ = millis() + 10000;
    yuleLog("leds", "all %u LEDs full white at 100%% for 10 s; the %u mA power budget still applies",
            static_cast<unsigned>(YULE_LED_COUNT), static_cast<unsigned>(YULE_LED_MAX_MA));
  } else if (command == "sweep") {
    yuleLog("leds", "lighting each of the %u LEDs in turn, first to last", static_cast<unsigned>(YULE_LED_COUNT));
    leds_.sweep();
  } else if (command == "help") {
    yuleLog("setup", "commands: password <value> | status | white | sweep | help");
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
  const yule::SoundContext ctx = soundContext();
  yuleLog("status", "sound: %s%s; playing: %s, %u waiting", profile_.muted ? "muted" : "on",
          yule::inQuietHours(ctx.quiet, ctx.clockSynced, ctx.minuteOfDay) ? " (quiet hours now)" : "",
          scheduler_.current() ? scheduler_.current()->message.name.c_str() : "nothing",
          static_cast<unsigned>(scheduler_.waiting()));
  const yule::UpdateView u = updateView();
  yuleLog("status", "update: %s %s %s", yule::updateStateName(u.state), u.version.c_str(), u.detail.c_str());
}
