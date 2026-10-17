// The owner's saved settings (profile) and the device-setup values (ident), with a versioned JSON form for
// NVS storage and the settings API. Hardware independent.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "pattern.h"
#include "quiet_hours.h"

namespace yule {

constexpr int kSettingsSchemaVersion = 1;

struct SongChoice {
  enum class Kind { Library, Custom };
  Kind kind = Kind::Library;
  std::string libraryId;  // used when kind == Library
  std::string rtttl;      // used when kind == Custom
};

struct TimeZoneSetting {
  std::string name = "UTC";   // IANA name from the table, or "custom"
  std::string rule = "UTC0";  // POSIX TZ rule actually applied
};

struct Profile {
  std::string displayName = "Family member";
  SongChoice song;            // the first built-in carol until the owner chooses
  Pattern pattern;            // breathe, warm white, medium
  uint8_t brightness = 50;    // percent, 5-100
  bool muted = false;
  QuietHours quiet;           // disabled until the owner turns it on
  TimeZoneSetting timeZone;

  bool operator==(const Profile& o) const;
};

// A device with no saved profile: "Family member", the first carol, breathe in warm white, 50%.
Profile defaultProfile();

// The RTTTL to play for a song choice. An unknown library id falls back to the default carol.
std::string resolveSongRtttl(const SongChoice& song);

// {"schema":1,"name":..., "song":{...}, "pattern":{...}, "brightness":50, "muted":false,
//  "quiet":{"enabled":false,"start":"21:30","end":"07:00"}, "tz":{"name":"UTC","rule":"UTC0"}}
std::string encodeProfile(const Profile& profile);

// Loads a saved profile. Never fails: anything missing, mistyped or out of range (an older or damaged blob)
// falls back to the default for that field.
Profile decodeProfile(std::string_view json);

// Device-setup values kept in the `ident` namespace. The password is write-only: it is never shown in any
// page or API response.
struct Ident {
  std::string password;
  std::string brokerHost;  // empty = use the build-time default
  uint16_t brokerPort = 0; // 0 = use the build-time default

  bool hasPassword() const { return !password.empty(); }
};

std::string encodeIdent(const Ident& ident);
Ident decodeIdent(std::string_view json);

}  // namespace yule
