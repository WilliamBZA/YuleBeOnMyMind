// Build-time configuration for the decoration firmware (design D11).
//
// Every value here can be overridden per board from platformio.ini with a -D flag, for example
//   build_flags = -DYULE_PIN_LED=5 -DYULE_LED_COUNT=60
// Values fixed by the specs (button thresholds, 30 s playback cap, staleness window, ...) live in
// lib/yule_core/yule_limits.h instead, because the hardware-independent core must not include this file.
#pragma once

#include <stdint.h>

// secrets.h carries the values that differ between deployments (broker port, release public keys,
// optional CA). It is git-ignored; copy secrets.example.h to secrets.h and edit it.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "firmware/include/secrets.h is missing. Copy firmware/include/secrets.example.h to firmware/include/secrets.h and edit it (broker port, release public key)."
#endif

// ---- Identity of this build -------------------------------------------------------------------

#ifndef YULE_FW_VERSION
#define YULE_FW_VERSION "0.0.0-dev"
#endif

// Board type reported in status and matched against firmware manifests: esp32dev, esp32-c3, esp32-s3.
#ifndef YULE_BOARD_ID
#define YULE_BOARD_ID "esp32dev"
#endif

// ---- Pins (classic ESP32 DevKit defaults; strapping pins avoided) -----------------------------

#ifndef YULE_PIN_LED
#define YULE_PIN_LED 13
#endif
#ifndef YULE_PIN_BUZZER
#define YULE_PIN_BUZZER 25
#endif
#ifndef YULE_PIN_BUTTON
#define YULE_PIN_BUTTON 27  // momentary switch to GND, internal pull-up
#endif

// ---- LED strip ---------------------------------------------------------------------------------

#ifndef YULE_LED_COUNT
#define YULE_LED_COUNT 12  // 1..300
#endif
#ifndef YULE_LED_COLOR_ORDER
#define YULE_LED_COLOR_ORDER GRB  // FastLED EOrder token, e.g. GRB for most WS2812B strips
#endif
#ifndef YULE_LED_VOLTS
#define YULE_LED_VOLTS 5
#endif
// Power budget handed to FastLED. 450 mA keeps a breadboard on USB (about 500 mA) within limits.
#ifndef YULE_LED_MAX_MA
#define YULE_LED_MAX_MA 450
#endif
#ifndef YULE_FRAME_INTERVAL_MS
#define YULE_FRAME_INTERVAL_MS 20  // 50 frames per second
#endif

// ---- Behaviour ---------------------------------------------------------------------------------

// After a successful send, further short presses are ignored for this long.
#ifndef YULE_COOLDOWN_MS
#define YULE_COOLDOWN_MS 30000
#endif

// ---- Messaging ---------------------------------------------------------------------------------

#ifndef YULE_TOPIC_ROOT
#define YULE_TOPIC_ROOT "yule/v1"
#endif
#ifndef YULE_BROKER_HOST
#define YULE_BROKER_HOST "williamb.net"
#endif
// YULE_BROKER_PORT comes from secrets.h.

// ---- Timing of the connection and setup state machines -----------------------------------------

#ifndef YULE_NTP_WAIT_MS
#define YULE_NTP_WAIT_MS 15000  // wait this long for SNTP before connecting to the broker anyway
#endif
#ifndef YULE_BACKOFF_BASE_MS
#define YULE_BACKOFF_BASE_MS 1000
#endif
#ifndef YULE_BACKOFF_CAP_MS
#define YULE_BACKOFF_CAP_MS 60000  // Wi-Fi and broker retries never wait longer than this
#endif
#ifndef YULE_CONNECTING_INDICATION_MS
#define YULE_CONNECTING_INDICATION_MS 60000  // amber pulse until connected or this long after boot
#endif
#ifndef YULE_CONNECTION_LOST_AFTER_MS
#define YULE_CONNECTION_LOST_AFTER_MS 60000  // red blink once the broker has been gone this long
#endif
#ifndef YULE_PORTAL_TIMEOUT_MS
#define YULE_PORTAL_TIMEOUT_MS (10UL * 60UL * 1000UL)  // Wi-Fi setup portal closes after 10 minutes
#endif
#ifndef YULE_WIFI_ATTEMPT_TIMEOUT_MS
#define YULE_WIFI_ATTEMPT_TIMEOUT_MS 20000  // give up on one Wi-Fi connection attempt after this long
#endif
#ifndef YULE_MQTT_KEEPALIVE_S
#define YULE_MQTT_KEEPALIVE_S 30  // the broker publishes the offline Last Will about 1.5x this after a power cut
#endif
#ifndef YULE_MQTT_CONNECT_WATCHDOG_MS
#define YULE_MQTT_CONNECT_WATCHDOG_MS 30000  // abandon a broker connection attempt that never answers
#endif
