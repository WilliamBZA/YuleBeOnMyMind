// Hardware-derived device identity: "d-" plus the 12 lower-case hex digits of the factory MAC address.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace yule {

// mac48 holds the MAC as a number, most significant byte first (3C:71:BF:4A:2B:10 is 0x3C71BF4A2B10).
// Bits above 48 are ignored.
std::string deviceIdFromMac(uint64_t mac48);

// The same from the 6 bytes in transmission order, as esp_efuse_mac_get_default() returns them.
std::string deviceIdFromMacBytes(const uint8_t mac[6]);

// True for exactly "d-" followed by 12 lower-case hex digits.
bool isValidDeviceId(std::string_view id);

// The last 6 hex digits of a valid device id, used in the setup access point and mDNS names.
// Returns an empty string for an invalid id.
std::string deviceSuffix(std::string_view id);

}  // namespace yule
