// The application: owns the drivers, the saved state and the connection state machines, and runs them from
// one non-blocking tick (design D3). All decisions are made by the hardware-independent core; this class
// feeds it inputs and carries out what it decides.
//
// Built with -DYULE_BRINGUP=1 (env `bringup`), it runs without any networking for breadboard bring-up: a
// short press plays the owner's own song and pattern, a long press toggles mute.
#pragma once

#include <Arduino.h>

#include <memory>
#include <string>

#include "button_driver.h"
#include "button_gestures.h"
#include "buzzer_driver.h"
#include "compositor.h"
#include "connection_policy.h"
#include "cooldown.h"
#include "cues.h"
#include "led_driver.h"
#include "melody_player.h"
#include "membership_config.h"
#include "mqtt_link.h"
#include "ota_client.h"
#include "ota_manifest.h"
#include "ota_policy.h"
#include "playback_queue.h"
#include "send_tracker.h"
#include "serial_console.h"
#include "settings_api.h"
#include "settings_model.h"
#include "status_indication.h"
#include "storage.h"
#include "subscription_plan.h"
#include "time_sync.h"
#include "topics.h"
#include "web_server.h"
#include "wifi_manager_glue.h"
#include "yule_config.h"

#ifndef YULE_BRINGUP
#define YULE_BRINGUP 0
#endif

class App : public SettingsApi {
 public:
  App();

  void begin();
  void loop();

  // SettingsApi (the settings page)
  std::string statusJson() override;
  std::string settingsJson() override;
  std::string saveSettings(const std::string& body, bool& ok) override;
  std::string preview(const std::string& body, bool& ok) override;
  std::string saveDevice(const std::string& body, bool& ok) override;

 private:
  // Boot
  void checkFirmwareOnBoot();

  // One step of the tick each, in the order of design D3.
  void tickButton(uint32_t nowMs);
  void tickWifi(uint32_t nowMs);
  void tickClock(uint32_t nowMs);
  void tickBroker(uint32_t nowMs);
  void tickSend(uint32_t nowMs);
  void tickPlayback(uint32_t nowMs);
  void tickSound(uint32_t nowMs);
  void tickIndication(uint32_t nowMs);
  void tickRender(uint32_t nowMs);
  void tickWeb();
  void tickOta(uint32_t nowMs);
  void tickHealth(uint32_t nowMs);
  void tickConsole();

  // Button actions
  void onControl(yule::ControlAction action, uint32_t nowMs);
  void sendPress(uint32_t nowMs);
  void finishSend(bool success, uint32_t nowMs);
  void toggleMute(uint32_t nowMs);
  void playOwnPatternLocally(uint32_t nowMs);  // bring-up

  // Feedback
  void showCue(yule::Cue cue, uint32_t nowMs);
  void playTone(const yule::Melody& tone, uint32_t nowMs);
  yule::SoundContext soundContext() const;

  // Broker events
  void onMqttEvent(const MqttEvent& event, uint32_t nowMs);
  void onConnected(bool sessionPresent, uint32_t nowMs);
  void onMessage(const MqttEvent& event, uint32_t nowMs);
  void onPress(const MqttEvent& event, uint32_t nowMs);
  void onConfigMessage(const std::string& payload);
  void onManifest(const std::string& payload);
  void applySubscriptionChange(const yule::SubscriptionChange& change);
  void subscribeToEverything();
  void publishOnlineStatus();
  void configureBroker();

  // Updates
  bool idleForUpdate() const;
  yule::UpdateView updateView() const;
  void revertFirmware(const char* why);

  // Console commands
  void onConsoleLine(const std::string& line);
  void printStatus();

  void startMdns();
  std::string newMessageId();
  yule::PlaybackItem makeItem(const yule::PressMessage& message, bool preview) const;

  Storage storage_;
  WifiGlue wifi_;
  TimeSync time_;
  MqttLink mqtt_;
  yule::Topics topics_;
  SettingsServer web_;
  OtaClient ota_;

  LedDriver leds_;
  BuzzerDriver buzzer_;
  ButtonDriver button_;
  yule::ButtonGestures gestures_;
  yule::ControlLogic controls_;
  yule::Compositor compositor_;
  SerialConsole console_;

  yule::Profile profile_;
  yule::Ident ident_;
  yule::Membership membership_;  // the last config the server sent (or an empty one)
  yule::OtaState otaState_;

  std::string deviceId_;
  std::string suffix_;
  std::string hostname_;

  // Sending and receiving
  yule::SendTracker tracker_;
  bool sendPublished_ = false;  // the current send reached the broker link (so a failure means a timeout)
  std::unique_ptr<yule::PressReceiver> receiver_;
  yule::PlaybackScheduler scheduler_;
  yule::MelodyPlayer songPlayer_;
  yule::MelodyPlayer tonePlayer_;
  bool songIsPreview_ = false;
  yule::Melody toneSent_;
  yule::Melody toneFailed_;
  yule::Melody toneUnmuted_;
  yule::Melody chime_;

  // LEDs
  yule::Rgb frame_[YULE_LED_COUNT];
  uint32_t lastFrameMs_ = 0;
  bool holdCueShown_ = false;
  bool whiteTest_ = false;  // the `white` serial command: full white at 100% for the power-budget check
  uint32_t whiteTestUntilMs_ = 0;
  bool playbackOwnsBase_ = false;
  bool indicationShown_ = false;
  yule::Indication indication_ = yule::Indication::None;
  std::shared_ptr<yule::UpdateProgressIndication> progress_;

  // Local time for quiet hours, refreshed once a second
  uint32_t clockCheckedMs_ = 0;
  bool clockWasSynced_ = false;
  int minuteOfDay_ = 0;

  // Connection state
  bool wifiWasUp_ = false;
  yule::BrokerGate gate_ = yule::BrokerGate::NoWifi;
  yule::BrokerGate lastGate_ = yule::BrokerGate::NoWifi;
  bool gateLogged_ = false;
  bool everConnected_ = false;
  uint32_t disconnectedSinceMs_ = 0;  // boot, until the first connection

  // Updates
  bool releaseKeysUsable_ = false;
  bool hasManifest_ = false;
  yule::OtaManifest manifest_;
  yule::InstallDecision lastDecision_ = yule::InstallDecision::SkipNotNewer;
  bool decisionLogged_ = false;
  uint32_t otaCheckedMs_ = 0;
  uint32_t otaRetryAtMs_ = 0;
  bool otaPausedBroker_ = false;
  int otaLoggedPercent_ = -1;
  yule::HealthMonitor health_;
  bool healthWifiUp_ = false;
};
