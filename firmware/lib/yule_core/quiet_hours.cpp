#include "quiet_hours.h"

namespace yule {

bool inQuietHours(const QuietHours& quiet, bool clockSynced, int minuteOfDay) {
  if (!quiet.enabled || !clockSynced) return false;
  if (quiet.startMinute == quiet.endMinute) return false;
  if (quiet.startMinute < quiet.endMinute) {
    return minuteOfDay >= quiet.startMinute && minuteOfDay < quiet.endMinute;
  }
  // Crosses midnight: inside from the start to midnight, and from midnight to the end.
  return minuteOfDay >= quiet.startMinute || minuteOfDay < quiet.endMinute;
}

bool soundAllowed(const SoundContext& ctx) {
  if (ctx.preview) return true;
  if (ctx.muted) return false;
  return !inQuietHours(ctx.quiet, ctx.clockSynced, ctx.minuteOfDay);
}

int parseClockTime(std::string_view text) {
  if (text.size() != 5 || text[2] != ':') return -1;
  auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : -1; };
  const int h1 = digit(text[0]), h2 = digit(text[1]), m1 = digit(text[3]), m2 = digit(text[4]);
  if (h1 < 0 || h2 < 0 || m1 < 0 || m2 < 0) return -1;
  const int hours = h1 * 10 + h2;
  const int minutes = m1 * 10 + m2;
  if (hours > 23 || minutes > 59) return -1;
  return hours * 60 + minutes;
}

std::string formatClockTime(int minuteOfDay) {
  if (minuteOfDay < 0 || minuteOfDay >= kMinutesPerDay) minuteOfDay = 0;
  const int h = minuteOfDay / 60, m = minuteOfDay % 60;
  std::string s = "00:00";
  s[0] = static_cast<char>('0' + h / 10);
  s[1] = static_cast<char>('0' + h % 10);
  s[3] = static_cast<char>('0' + m / 10);
  s[4] = static_cast<char>('0' + m % 10);
  return s;
}

}  // namespace yule
