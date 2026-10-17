// Built-in public-domain carols, plus the short feedback tones and the default chime.
// All are RTTTL strings (see rtttl.h). The carols are short, simplified opening melodies of well-known
// public-domain tunes; every one plays for 30 seconds or less.
#pragma once

#include <cstddef>
#include <string_view>

namespace yule {

struct SongInfo {
  const char* id;     // stable identifier stored in the profile, e.g. "jingle-bells"
  const char* title;  // shown on the settings page
  const char* rtttl;
};

// Number of built-in carols (at least eight).
size_t carolCount();
const SongInfo& carolAt(size_t index);
// Returns nullptr if there is no carol with that id.
const SongInfo* findCarol(std::string_view id);
// The first carol: the default song for a device with no saved profile.
const SongInfo& defaultCarol();

// Feedback tones. They follow the same do-not-disturb rules as songs.
inline constexpr const char* kToneSent = "Sent:d=16,o=6,b=180:e,8b";
inline constexpr const char* kToneSendFailed = "Failed:d=16,o=4,b=140:e,p,e,p,4c";
inline constexpr const char* kToneMutedOff = "Unmuted:d=16,o=6,b=200:e,a,8c7";
// Used when a received song is missing, invalid or too long.
inline constexpr const char* kDefaultChime = "Chime:d=8,o=6,b=150:c,e,g,4c7";

}  // namespace yule
