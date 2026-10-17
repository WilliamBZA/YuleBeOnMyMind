// Presence payloads published on <root>/devices/<id>/status (design D4):
//   online : {"v":1,"state":"online","fw":"0.3.0","board":"esp32dev","name":"Gran","ota":"ok"}
//   offline: {"v":1,"state":"offline"}   (the retained Last Will)
#pragma once

#include <string>

namespace yule {

struct StatusInfo {
  std::string firmwareVersion;
  std::string board;
  std::string displayName;
  std::string otaResult = "none";  // "none", "ok", "failed:<reason>" or "rolled-back:<version>"
};

std::string encodeStatusOnline(const StatusInfo& info);
std::string encodeStatusOffline();

}  // namespace yule
