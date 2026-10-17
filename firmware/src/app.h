// The application: owns the drivers, the saved state and the connection state machines, and runs them from
// one non-blocking tick (design D3). Sending, receiving and playback are added to this class by the
// messaging tasks (14.x); this version covers identity, Wi-Fi, time, the broker link and membership.
#pragma once

#include <Arduino.h>

#include <memory>
#include <string>

#include "button_driver.h"
#include "button_gestures.h"
#include "compositor.h"
#include "connection_policy.h"
#include "led_driver.h"
#include "membership_config.h"
#include "mqtt_link.h"
#include "ota_policy.h"
#include "serial_console.h"
#include "settings_model.h"
#include "storage.h"
#include "subscription_plan.h"
#include "time_sync.h"
#include "topics.h"
#include "wifi_manager_glue.h"
#include "yule_config.h"

class App {
 public:
  App() : wifi_(storage_), topics_(YULE_TOPIC_ROOT) {}

  void begin();
  void loop();

 private:
  // One step of the tick each, in the order of design D3.
  void tickButton(uint32_t nowMs);
  void tickWifi(uint32_t nowMs);
  void tickClock();
  void tickBroker(uint32_t nowMs);
  void tickRender(uint32_t nowMs);
  void tickConsole();

  // Broker events
  void onMqttEvent(const MqttEvent& event);
  void onConnected(bool sessionPresent);
  void onMessage(const MqttEvent& event);
  void onConfigMessage(const std::string& payload);
  void applySubscriptionChange(const yule::SubscriptionChange& change);
  void subscribeToEverything();
  void publishOnlineStatus();
  void configureBroker();

  // Console commands
  void onConsoleLine(const std::string& line);
  void printStatus();

  void startMdns();

  Storage storage_;
  WifiGlue wifi_;
  TimeSync time_;
  MqttLink mqtt_;
  yule::Topics topics_;

  LedDriver leds_;
  ButtonDriver button_;
  yule::ButtonGestures gestures_;
  yule::Compositor compositor_;
  SerialConsole console_;

  yule::Profile profile_;
  yule::Ident ident_;
  yule::Membership membership_;  // the last config the server sent (or an empty one)
  yule::OtaState otaState_;

  std::string deviceId_;
  std::string suffix_;
  std::string hostname_;

  yule::Rgb frame_[YULE_LED_COUNT];
  uint32_t lastFrameMs_ = 0;
  bool setupShown_ = false;

  bool wifiWasUp_ = false;
  bool clockWasSynced_ = false;
  yule::BrokerGate lastGate_ = yule::BrokerGate::NoWifi;
  bool gateLogged_ = false;
};
