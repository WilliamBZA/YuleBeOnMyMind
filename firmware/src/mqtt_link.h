// Broker connection on the ESP-IDF esp_mqtt client (design D3, D4, D5).
//
// esp_mqtt runs in its own FreeRTOS task and does the TLS handshake, keep-alive and QoS 1 acknowledgements
// there, so a slow handshake never stalls the animations. Its events are copied onto a queue; update()
// drains that queue on the main loop, so only the main loop ever touches application state.
//
//  - TLS with the certificate bundle (or one pinned PEM) and hostname verification: a broker the
//    certificate does not cover is refused before any credentials are sent.
//  - username and client id are the device id; clean_session is off so the broker queues QoS 1 presses
//    during short outages; a retained offline Last Will is set.
//  - Reconnection is driven here, with the core's back-off (capped at 60 s), not by esp_mqtt's fixed timer.
//  - Connection problems are written to the serial log here, with the reason.
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <mqtt_client.h>

#include <atomic>
#include <deque>
#include <string>

#include "backoff.h"

struct MqttSettings {
  std::string host;
  uint16_t port = 0;
  std::string username;  // the device id
  std::string clientId;  // the device id
  std::string password;
  std::string willTopic;    // retained offline status
  std::string willPayload;
};

struct MqttEvent {
  enum class Type : uint8_t {
    Connected,     // the broker accepted the login: subscribe and announce now
    Disconnected,  // an established connection ended
    Message,       // a publish arrived on a subscribed topic
    Published,     // the broker acknowledged a QoS 1 publish (msgId)
  };
  Type type = Type::Disconnected;
  bool sessionPresent = false;  // Connected: the broker kept our subscriptions from last time
  bool retained = false;        // Message
  int msgId = 0;                // Published
  std::string topic;            // Message
  std::string payload;          // Message
};

class MqttLink {
 public:
  MqttLink();
  ~MqttLink();
  MqttLink(const MqttLink&) = delete;
  MqttLink& operator=(const MqttLink&) = delete;

  // Stores the connection details. They take effect at the next connection; call reconnect() to apply them now.
  void configure(const MqttSettings& settings);

  // Call every loop tick. `allowed` is the broker gate: false drops the connection (and keeps it dropped)
  // until it is true again.
  void update(uint32_t nowMs, bool allowed);

  // Pops the next event for the application; returns false when there is none.
  bool poll(MqttEvent& out);

  bool connected() const { return state_ == State::Connected; }
  uint32_t connectedForMs(uint32_t nowMs) const { return connected() ? nowMs - connectedSinceMs_ : 0; }
  const char* stateName() const;

  // Drops the connection and connects again straight away with the current settings.
  void reconnect();

  // All return the MQTT message id (>= 0), or -1 when not connected or the request failed. They do not block.
  int publish(const std::string& topic, const std::string& payload, int qos, bool retain);
  int subscribe(const std::string& topic, int qos = 1);
  int unsubscribe(const std::string& topic);

 private:
  enum class State : uint8_t { Idle, Connecting, Connected };

  // What the esp_mqtt task hands to the main loop.
  struct Raw {
    enum class Kind : uint8_t { Event, LinkDown, Notice };
    Kind kind = Kind::Notice;
    MqttEvent event;          // Kind::Event (Connected, Message, Published)
    std::string text;         // Kind::LinkDown: why; Kind::Notice: what happened
    uint32_t generation = 0;  // which client produced it; stale ones are dropped
  };

  static void onEvent(void* arg, esp_event_base_t base, int32_t eventId, void* eventData);
  void handleInTask(esp_mqtt_event_handle_t e);  // runs on the esp_mqtt task
  void push(Raw* raw);

  void startClient(uint32_t nowMs);
  void teardown();
  void failConnection(uint32_t nowMs, const std::string& why);

  MqttSettings settings_;
  esp_mqtt_client_handle_t client_ = nullptr;
  QueueHandle_t queue_ = nullptr;
  std::atomic<uint32_t> generation_{1};

  State state_ = State::Idle;
  uint32_t retryAtMs_ = 0;
  uint32_t attemptStartMs_ = 0;
  uint32_t connectedSinceMs_ = 0;
  yule::Backoff backoff_;
  std::deque<MqttEvent> out_;

  // Reassembly of a message delivered in fragments (only the esp_mqtt task touches these).
  std::string rxTopic_;
  std::string rxPayload_;
  bool rxRetained_ = false;
  bool rxTooLarge_ = false;
};
