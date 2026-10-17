#include "status_payload.h"

#include <ArduinoJson.h>

#include "yule_limits.h"

namespace yule {

std::string encodeStatusOnline(const StatusInfo& info) {
  JsonDocument doc;
  doc["v"] = limits::kProtocolVersion;
  doc["state"] = "online";
  doc["fw"] = info.firmwareVersion;
  doc["board"] = info.board;
  doc["name"] = info.displayName;
  doc["ota"] = info.otaResult;
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string encodeStatusOffline() {
  JsonDocument doc;
  doc["v"] = limits::kProtocolVersion;
  doc["state"] = "offline";
  std::string out;
  serializeJson(doc, out);
  return out;
}

}  // namespace yule
