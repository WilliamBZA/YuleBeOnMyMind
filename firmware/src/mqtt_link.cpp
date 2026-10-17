#include "mqtt_link.h"

#include <esp_crt_bundle.h>
#include <esp_random.h>

#include <memory>
#include <new>

#include "log.h"
#include "yule_config.h"

namespace {

constexpr int kBufferBytes = 2048;
constexpr int kTaskStackBytes = 8192;     // TLS handshakes need more than esp_mqtt's default
constexpr int kNetworkTimeoutMs = 8000;
constexpr size_t kQueueLength = 32;       // events waiting for the main loop
constexpr size_t kMaxPayloadBytes = 6144; // the broker caps packets at 4096; anything larger is dropped
constexpr bool kHasPinnedCa = sizeof(YULE_BROKER_CA_PEM) > 1;

bool reached(uint32_t nowMs, uint32_t deadlineMs) { return static_cast<int32_t>(nowMs - deadlineMs) >= 0; }

// Describes an esp_mqtt error event for the serial log and says whether it ended the connection.
std::string describeError(const esp_mqtt_error_codes_t* codes, bool& linkDown) {
  linkDown = false;
  if (!codes) return "unknown MQTT error";
  char buf[200];
  switch (codes->error_type) {
    case MQTT_ERROR_TYPE_TCP_TRANSPORT:
      linkDown = true;
      if (codes->esp_tls_cert_verify_flags != 0) {
        snprintf(buf, sizeof(buf),
                 "TLS: the broker's certificate was rejected (verify flags 0x%x) - wrong host, expired, "
                 "or not signed by a trusted authority; no credentials were sent",
                 codes->esp_tls_cert_verify_flags);
      } else {
        snprintf(buf, sizeof(buf), "network/TLS error: %s (tls 0x%x, socket errno %d)",
                 esp_err_to_name(codes->esp_tls_last_esp_err), codes->esp_tls_stack_err,
                 codes->esp_transport_sock_errno);
      }
      return buf;
    case MQTT_ERROR_TYPE_CONNECTION_REFUSED:
      linkDown = true;
      switch (codes->connect_return_code) {
        case MQTT_CONNECTION_REFUSE_BAD_USERNAME:
        case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED:
          return "the broker refused the login: wrong password, or this device is not registered on the server";
        case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE:
          return "the broker refused the login: server unavailable";
        default:
          snprintf(buf, sizeof(buf), "the broker refused the login (code %d)", static_cast<int>(codes->connect_return_code));
          return buf;
      }
    case MQTT_ERROR_TYPE_SUBSCRIBE_FAILED:
      return "the broker refused a subscription (this device is not allowed that topic)";
    default:
      return "MQTT error";
  }
}

}  // namespace

MqttLink::MqttLink() : backoff_(YULE_BACKOFF_BASE_MS, YULE_BACKOFF_CAP_MS, esp_random()) {
  queue_ = xQueueCreate(kQueueLength, sizeof(Raw*));
}

MqttLink::~MqttLink() {
  teardown();
  if (queue_) {
    Raw* raw = nullptr;
    while (xQueueReceive(queue_, &raw, 0) == pdTRUE) delete raw;
    vQueueDelete(queue_);
  }
}

void MqttLink::configure(const MqttSettings& settings) { settings_ = settings; }

const char* MqttLink::stateName() const {
  switch (state_) {
    case State::Idle: return "idle";
    case State::Connecting: return "connecting";
    case State::Connected: return "connected";
  }
  return "?";
}

void MqttLink::update(uint32_t nowMs, bool allowed) {
  // 1. Take what the esp_mqtt task queued since last time.
  Raw* raw = nullptr;
  while (queue_ && xQueueReceive(queue_, &raw, 0) == pdTRUE) {
    std::unique_ptr<Raw> r(raw);
    if (r->generation != generation_.load()) continue;  // from a client we already dropped

    switch (r->kind) {
      case Raw::Kind::Notice:
        yuleLog("mqtt", "%s", r->text.c_str());
        break;
      case Raw::Kind::LinkDown:
        failConnection(nowMs, r->text);
        break;
      case Raw::Kind::Event:
        if (r->event.type == MqttEvent::Type::Connected) {
          state_ = State::Connected;
          connectedSinceMs_ = nowMs;
          backoff_.reset();
          yuleLog("mqtt", "connected to %s:%u as %s (broker %s our session)", settings_.host.c_str(),
                  static_cast<unsigned>(settings_.port), settings_.username.c_str(),
                  r->event.sessionPresent ? "kept" : "did not have");
        }
        out_.push_back(std::move(r->event));
        break;
    }
  }

  // 2. Not allowed (no Wi-Fi, no password, clock still syncing): stay down.
  if (!allowed) {
    if (client_) {
      const bool wasConnected = state_ == State::Connected;
      teardown();
      if (wasConnected) {
        yuleLog("mqtt", "connection closed (Wi-Fi lost or settings changed)");
        MqttEvent ev;
        ev.type = MqttEvent::Type::Disconnected;
        out_.push_back(std::move(ev));
      }
    }
    state_ = State::Idle;
    retryAtMs_ = nowMs;  // connect at once when allowed again
    return;
  }

  // 3. An attempt that never answers is abandoned, so the back-off can run.
  if (state_ == State::Connecting && nowMs - attemptStartMs_ >= YULE_MQTT_CONNECT_WATCHDOG_MS) {
    failConnection(nowMs, "no answer from the broker");
  }

  // 4. Start an attempt when one is due.
  if (state_ == State::Idle && reached(nowMs, retryAtMs_)) startClient(nowMs);
}

bool MqttLink::poll(MqttEvent& out) {
  if (out_.empty()) return false;
  out = std::move(out_.front());
  out_.pop_front();
  return true;
}

void MqttLink::reconnect() {
  if (client_) teardown();
  state_ = State::Idle;
  retryAtMs_ = millis();
  backoff_.reset();
}

void MqttLink::startClient(uint32_t nowMs) {
  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.hostname = settings_.host.c_str();
  cfg.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
  cfg.broker.address.port = settings_.port;
  // Verify the broker before sending credentials, including that the certificate covers the host name.
  if (kHasPinnedCa) {
    cfg.broker.verification.certificate = YULE_BROKER_CA_PEM;  // NUL-terminated PEM; not copied by esp_mqtt
  } else {
    cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
  }
  cfg.credentials.username = settings_.username.c_str();
  cfg.credentials.client_id = settings_.clientId.c_str();
  cfg.credentials.authentication.password = settings_.password.c_str();
  cfg.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
  cfg.session.disable_clean_session = true;  // keep the session so the broker queues QoS 1 presses meanwhile
  cfg.session.keepalive = YULE_MQTT_KEEPALIVE_S;
  cfg.session.last_will.topic = settings_.willTopic.c_str();
  cfg.session.last_will.msg = settings_.willPayload.c_str();
  cfg.session.last_will.msg_len = static_cast<int>(settings_.willPayload.size());
  cfg.session.last_will.qos = 1;
  cfg.session.last_will.retain = 1;
  cfg.network.disable_auto_reconnect = true;  // reconnection is paced by our own back-off
  cfg.network.timeout_ms = kNetworkTimeoutMs;
  cfg.buffer.size = kBufferBytes;
  cfg.task.stack_size = kTaskStackBytes;

  client_ = esp_mqtt_client_init(&cfg);
  if (!client_) {
    failConnection(nowMs, "could not create the MQTT client (out of memory?)");
    return;
  }
  esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &MqttLink::onEvent, this);
  state_ = State::Connecting;
  attemptStartMs_ = nowMs;
  yuleLog("mqtt", "connecting to %s:%u over TLS (%u bytes of heap free)", settings_.host.c_str(),
          static_cast<unsigned>(settings_.port), static_cast<unsigned>(ESP.getFreeHeap()));
  if (esp_mqtt_client_start(client_) != ESP_OK) failConnection(nowMs, "could not start the MQTT client");
}

void MqttLink::teardown() {
  if (!client_) return;
  esp_mqtt_client_stop(client_);  // waits for the esp_mqtt task to finish
  esp_mqtt_client_destroy(client_);
  client_ = nullptr;
  generation_.fetch_add(1);  // anything the old client still queued is now stale
  rxTopic_.clear();
  rxPayload_.clear();
}

void MqttLink::failConnection(uint32_t nowMs, const std::string& why) {
  const bool wasConnected = state_ == State::Connected;
  teardown();
  state_ = State::Idle;
  const uint32_t wait = backoff_.nextDelayMs();
  retryAtMs_ = nowMs + wait;
  yuleLog("mqtt", "%s: %s; trying again in %.1f s", wasConnected ? "connection lost" : "could not connect",
          why.c_str(), wait / 1000.0);
  if (wasConnected) {
    MqttEvent ev;
    ev.type = MqttEvent::Type::Disconnected;
    out_.push_back(std::move(ev));
  }
}

int MqttLink::publish(const std::string& topic, const std::string& payload, int qos, bool retain) {
  if (!client_ || state_ != State::Connected) return -1;
  // enqueue() hands the message to the esp_mqtt task and returns at once; QoS 1 messages are kept and
  // resent until acknowledged.
  return esp_mqtt_client_enqueue(client_, topic.c_str(), payload.data(), static_cast<int>(payload.size()), qos,
                                 retain ? 1 : 0, qos > 0);
}

int MqttLink::subscribe(const std::string& topic, int qos) {
  if (!client_ || state_ != State::Connected) return -1;
  return esp_mqtt_client_subscribe_single(client_, topic.c_str(), qos);
}

int MqttLink::unsubscribe(const std::string& topic) {
  if (!client_ || state_ != State::Connected) return -1;
  return esp_mqtt_client_unsubscribe(client_, topic.c_str());
}

// ---- esp_mqtt task ----------------------------------------------------------------------------------

void MqttLink::onEvent(void* arg, esp_event_base_t, int32_t, void* eventData) {
  static_cast<MqttLink*>(arg)->handleInTask(static_cast<esp_mqtt_event_handle_t>(eventData));
}

void MqttLink::push(Raw* raw) {
  if (!raw) return;
  raw->generation = generation_.load();
  if (!queue_ || xQueueSend(queue_, &raw, 0) != pdTRUE) delete raw;  // main loop far behind: drop
}

void MqttLink::handleInTask(esp_mqtt_event_handle_t e) {
  switch (e->event_id) {
    case MQTT_EVENT_CONNECTED: {
      Raw* r = new (std::nothrow) Raw;
      if (!r) return;
      r->kind = Raw::Kind::Event;
      r->event.type = MqttEvent::Type::Connected;
      r->event.sessionPresent = e->session_present != 0;
      push(r);
      break;
    }
    case MQTT_EVENT_DISCONNECTED: {
      Raw* r = new (std::nothrow) Raw;
      if (!r) return;
      r->kind = Raw::Kind::LinkDown;
      r->text = "connection closed";
      push(r);
      break;
    }
    case MQTT_EVENT_PUBLISHED: {
      Raw* r = new (std::nothrow) Raw;
      if (!r) return;
      r->kind = Raw::Kind::Event;
      r->event.type = MqttEvent::Type::Published;
      r->event.msgId = e->msg_id;
      push(r);
      break;
    }
    case MQTT_EVENT_ERROR: {
      bool linkDown = false;
      const std::string text = describeError(e->error_handle, linkDown);
      Raw* r = new (std::nothrow) Raw;
      if (!r) return;
      r->kind = linkDown ? Raw::Kind::LinkDown : Raw::Kind::Notice;
      r->text = text;
      push(r);
      break;
    }
    case MQTT_EVENT_DATA: {
      if (e->current_data_offset == 0) {  // the topic travels with the first fragment only
        rxTopic_.assign(e->topic ? e->topic : "", e->topic ? static_cast<size_t>(e->topic_len) : 0);
        rxPayload_.clear();
        rxRetained_ = e->retain;
        rxTooLarge_ = static_cast<size_t>(e->total_data_len) > kMaxPayloadBytes;
        if (!rxTooLarge_) rxPayload_.reserve(static_cast<size_t>(e->total_data_len));
      }
      if (!rxTooLarge_ && e->data_len > 0) rxPayload_.append(e->data, static_cast<size_t>(e->data_len));
      if (e->current_data_offset + e->data_len >= e->total_data_len) {  // last fragment
        Raw* r = new (std::nothrow) Raw;
        if (r) {
          if (rxTooLarge_) {
            r->kind = Raw::Kind::Notice;
            r->text = "dropped an oversized message on " + rxTopic_;
          } else {
            r->kind = Raw::Kind::Event;
            r->event.type = MqttEvent::Type::Message;
            r->event.topic = std::move(rxTopic_);
            r->event.payload = std::move(rxPayload_);
            r->event.retained = rxRetained_;
          }
          push(r);
        }
        rxTopic_.clear();
        rxPayload_.clear();
        rxTooLarge_ = false;
      }
      break;
    }
    default:
      break;  // BEFORE_CONNECT, SUBSCRIBED, UNSUBSCRIBED, DELETED: nothing to do
  }
}
