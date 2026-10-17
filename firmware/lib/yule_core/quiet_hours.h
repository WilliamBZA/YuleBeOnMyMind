// Quiet hours and the rule that decides whether the buzzer may sound. Hardware independent: the caller works
// out the local minute of day from the clock and the owner's time-zone rule.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace yule {

constexpr int kMinutesPerDay = 24 * 60;

struct QuietHours {
  bool enabled = false;
  int startMinute = 21 * 60 + 30;  // 21:30, minutes after local midnight
  int endMinute = 7 * 60;          // 07:00, exclusive
};

// True while local time is inside the window. The window may cross midnight (21:30-07:00). The end is
// exclusive. start == end means no quiet hours. Quiet hours never apply while the clock is unsynchronised.
bool inQuietHours(const QuietHours& quiet, bool clockSynced, int minuteOfDay);

struct SoundContext {
  bool muted = false;
  QuietHours quiet;
  bool clockSynced = false;
  int minuteOfDay = 0;
  bool preview = false;  // an explicit local preview from the settings page
};

// May the buzzer sound? Mute and quiet hours silence everything (songs and feedback tones alike); a
// preview is an explicit local request and always sounds.
bool soundAllowed(const SoundContext& ctx);

// "HH:MM" (24-hour) to minutes after midnight, or -1 if it is not a valid time.
int parseClockTime(std::string_view text);
std::string formatClockTime(int minuteOfDay);

}  // namespace yule
