#include "connection_policy.h"

namespace yule {

const char* brokerGateName(BrokerGate gate) {
  switch (gate) {
    case BrokerGate::Open: return "open";
    case BrokerGate::NoPassword: return "no broker password set (setup required)";
    case BrokerGate::NoWifi: return "no Wi-Fi";
    case BrokerGate::WaitingForClock: return "waiting for the clock";
  }
  return "?";
}

BrokerGate evaluateBrokerGate(const BrokerGateInputs& in) {
  if (!in.hasPassword) return BrokerGate::NoPassword;
  if (!in.wifiUp) return BrokerGate::NoWifi;
  if (!in.clockSynced && in.wifiUpForMs < in.clockWaitMs) return BrokerGate::WaitingForClock;
  return BrokerGate::Open;
}

}  // namespace yule
