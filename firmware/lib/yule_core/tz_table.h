// A curated list of common time zones mapped to POSIX TZ rules, so the settings page can offer a menu and
// the device needs no time-zone database. Daylight-saving changes come from the rule itself.
#pragma once

#include <cstddef>
#include <string_view>

namespace yule {

struct TimeZoneInfo {
  const char* name;  // IANA name, e.g. "Europe/London"
  const char* rule;  // POSIX TZ string, e.g. "GMT0BST,M3.5.0/1,M10.5.0"
};

size_t timeZoneCount();
const TimeZoneInfo& timeZoneAt(size_t index);
// nullptr when the name is not in the table (the owner can still enter a custom rule).
const TimeZoneInfo* findTimeZone(std::string_view name);
// The zone used before the owner chooses one.
const TimeZoneInfo& defaultTimeZone();

}  // namespace yule
