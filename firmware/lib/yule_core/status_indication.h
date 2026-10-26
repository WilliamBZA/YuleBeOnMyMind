// Which status indication the LED strip shows while nothing else is playing (device-controls, "Status
// indications"). Hardware independent; src/app turns the answer into a base-layer animation from cues.h.
#pragma once

#include <cstdint>

namespace yule {

enum class Indication : uint8_t {
  None,            // connected and idle: all LEDs off
  Setup,           // the Wi-Fi setup access point is open
  Connecting,      // after boot, until connected to the broker or 60 s have passed
  ConnectionLost,  // disconnected from the broker for more than 60 s
  Updating,        // a firmware update is downloading or installing
};

const char* indicationName(Indication indication);

struct IndicationInputs {
  bool updating = false;
  bool portalOpen = false;
  bool brokerConnected = false;
  bool everConnected = false;       // the broker has been connected at least once since boot
  uint32_t uptimeMs = 0;
  uint32_t disconnectedForMs = 0;   // since the broker connection was lost, or since boot if never connected
  uint32_t connectingForMs = 60000; // how long the connecting pulse may show after boot
  uint32_t lostAfterMs = 60000;     // how long the broker must be gone before the lost-connection blink
};

// Updating outranks everything; then setup; then the broker state. Message playback and feedback cues are
// layered on top of the result by the caller, except while updating.
Indication chooseIndication(const IndicationInputs& in);

}  // namespace yule
