#include "device_id.h"

namespace yule {

std::string deviceIdFromMac(uint64_t mac48) {
  static const char kHex[] = "0123456789abcdef";
  std::string id = "d-000000000000";
  for (int i = 0; i < 12; ++i) {
    const int shift = 4 * (11 - i);
    id[2 + i] = kHex[(mac48 >> shift) & 0xF];
  }
  return id;
}

std::string deviceIdFromMacBytes(const uint8_t mac[6]) {
  uint64_t v = 0;
  for (int i = 0; i < 6; ++i) v = (v << 8) | mac[i];
  return deviceIdFromMac(v);
}

bool isValidDeviceId(std::string_view id) {
  if (id.size() != 14 || id[0] != 'd' || id[1] != '-') return false;
  for (size_t i = 2; i < id.size(); ++i) {
    const char c = id[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

std::string deviceSuffix(std::string_view id) {
  if (!isValidDeviceId(id)) return std::string();
  return std::string(id.substr(id.size() - 6));
}

}  // namespace yule
