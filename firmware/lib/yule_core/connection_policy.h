// When the device may connect to the broker (device-provisioning), and whether the clock can be trusted.
// Hardware independent.
#pragma once

#include <cstdint>

namespace yule {

// ---- Clock ----

// The earliest epoch time treated as a real, synchronised clock (2025-01-01 UTC). An unsynchronised ESP32
// reports a time in 1970.
constexpr int64_t kMinSyncedEpochSeconds = 1735689600;

inline bool isClockSynced(int64_t epochSeconds) { return epochSeconds >= kMinSyncedEpochSeconds; }

// ---- Broker gate ----

// Why the broker connection is, or is not, allowed right now.
enum class BrokerGate : uint8_t {
  Open,             // connect (or stay connected)
  NoPassword,       // no broker password has been entered: never connect ("setup required")
  NoWifi,           // no network yet
  WaitingForClock,  // Wi-Fi is up but the clock is not synchronised yet; wait a little so staleness can be judged
};

const char* brokerGateName(BrokerGate gate);

struct BrokerGateInputs {
  bool hasPassword = false;
  bool wifiUp = false;
  uint32_t wifiUpForMs = 0;   // how long Wi-Fi has been up
  bool clockSynced = false;
  uint32_t clockWaitMs = 15000;  // give up waiting for the clock after this long
};

// A device without a password never connects. Otherwise it needs Wi-Fi, then either a synchronised clock
// or 15 s of waiting for one, so that presses can be checked for staleness (and the broker's certificate
// for validity).
BrokerGate evaluateBrokerGate(const BrokerGateInputs& in);

}  // namespace yule
