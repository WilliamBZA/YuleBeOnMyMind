#include "status_indication.h"

namespace yule {

const char* indicationName(Indication indication) {
  switch (indication) {
    case Indication::None: return "idle";
    case Indication::Setup: return "setup";
    case Indication::Connecting: return "connecting";
    case Indication::ConnectionLost: return "connection lost";
    case Indication::Updating: return "updating";
  }
  return "?";
}

Indication chooseIndication(const IndicationInputs& in) {
  if (in.updating) return Indication::Updating;
  if (in.portalOpen) return Indication::Setup;
  if (in.brokerConnected) return Indication::None;
  if (!in.everConnected && in.uptimeMs < in.connectingForMs) return Indication::Connecting;
  if (in.disconnectedForMs > in.lostAfterMs) return Indication::ConnectionLost;
  return Indication::None;  // a short outage: stay dark rather than alarm anyone
}

}  // namespace yule
