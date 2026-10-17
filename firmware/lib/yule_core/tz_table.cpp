#include "tz_table.h"

namespace yule {

namespace {

// Rules follow the tz database's POSIX strings. "<+03>-3" is the quoted-name form used for zones without an
// alphabetic abbreviation.
const TimeZoneInfo kZones[] = {
    {"UTC", "UTC0"},
    // Europe
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Dublin", "GMT0IST,M3.5.0/1,M10.5.0"},
    {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Istanbul", "<+03>-3"},
    {"Europe/Moscow", "MSK-3"},
    // Africa
    {"Africa/Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"Africa/Lagos", "WAT-1"},
    {"Africa/Nairobi", "EAT-3"},
    {"Africa/Johannesburg", "SAST-2"},
    // Middle East and Asia
    {"Asia/Jerusalem", "IST-2IDT,M3.4.4/26,M10.5.0"},
    {"Asia/Dubai", "<+04>-4"},
    {"Asia/Karachi", "PKT-5"},
    {"Asia/Kolkata", "IST-5:30"},
    {"Asia/Dhaka", "<+06>-6"},
    {"Asia/Bangkok", "<+07>-7"},
    {"Asia/Jakarta", "WIB-7"},
    {"Asia/Singapore", "<+08>-8"},
    {"Asia/Hong_Kong", "HKT-8"},
    {"Asia/Shanghai", "CST-8"},
    {"Asia/Manila", "PST-8"},
    {"Asia/Seoul", "KST-9"},
    {"Asia/Tokyo", "JST-9"},
    // Australia and the Pacific
    {"Australia/Perth", "AWST-8"},
    {"Australia/Darwin", "ACST-9:30"},
    {"Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Australia/Brisbane", "AEST-10"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Pacific/Honolulu", "HST10"},
    // Americas
    {"America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Phoenix", "MST7"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Mexico_City", "CST6"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Toronto", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Halifax", "AST4ADT,M3.2.0,M11.1.0"},
    {"America/St_Johns", "NST3:30NDT,M3.2.0,M11.1.0"},
    {"America/Bogota", "<-05>5"},
    {"America/Lima", "<-05>5"},
    {"America/Santiago", "<-04>4<-03>,M9.1.6/24,M4.1.6/24"},
    {"America/Sao_Paulo", "<-03>3"},
    {"America/Argentina/Buenos_Aires", "<-03>3"},
};

constexpr size_t kZoneCount = sizeof(kZones) / sizeof(kZones[0]);

}  // namespace

size_t timeZoneCount() { return kZoneCount; }

const TimeZoneInfo& timeZoneAt(size_t index) { return kZones[index < kZoneCount ? index : 0]; }

const TimeZoneInfo* findTimeZone(std::string_view name) {
  for (const TimeZoneInfo& z : kZones) {
    if (name == z.name) return &z;
  }
  return nullptr;
}

const TimeZoneInfo& defaultTimeZone() { return kZones[0]; }

}  // namespace yule
