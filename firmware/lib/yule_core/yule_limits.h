// Values fixed by the specs. Build-time tunables (pins, LED count, cooldown, ...) live in
// include/yule_config.h instead; the hardware-independent core must not depend on that file.
#pragma once

#include <cstddef>
#include <cstdint>

namespace yule {
namespace limits {

// ---- melody-playback ----
constexpr uint32_t kMelodyMaxMs = 30000;       // a melody stops after 30 s of playing time
constexpr size_t kMaxSongChars = 512;          // custom RTTTL / received song length limit

// ---- thinking-of-you-messaging ----
constexpr uint32_t kMinLightsMs = 10000;       // lights are shown for at least 10 s
constexpr uint32_t kFadeOutMs = 1000;          // LEDs fade out over about 1 s
constexpr uint32_t kQueueGapMs = 1000;         // pause between queued presses
constexpr size_t kQueueCapacity = 5;           // at most 5 presses waiting
constexpr size_t kRecentIdRing = 32;           // message ids remembered for de-duplication
constexpr int64_t kStaleAfterSeconds = 300;    // a press older than 5 minutes is discarded
constexpr uint32_t kAckTimeoutMs = 5000;       // every group copy must be acknowledged within 5 s
constexpr int kProtocolVersion = 1;

// ---- device-controls ----
constexpr uint32_t kDebounceMs = 25;
constexpr uint32_t kShortPressBelowMs = 1000;  // under 1 s: short press
constexpr uint32_t kLongPressAtMs = 2000;      // 2 s up to 10 s: long press
constexpr uint32_t kVeryLongPressAtMs = 10000; // 10 s or more: very long press (acts on reaching 10 s)
constexpr uint32_t kDefaultCooldownMs = 30000;

// ---- member-profile ----
constexpr size_t kMaxDisplayNameChars = 24;
constexpr size_t kMaxGroupNameChars = 40;
constexpr uint8_t kMinBrightnessPercent = 5;
constexpr uint8_t kMaxBrightnessPercent = 100;
constexpr uint8_t kDefaultBrightnessPercent = 50;
constexpr size_t kMaxPatternColors = 3;
constexpr uint8_t kMinSpeed = 1;
constexpr uint8_t kMaxSpeed = 3;
constexpr uint8_t kDefaultSpeed = 2;
constexpr size_t kMinLedCount = 1;
constexpr size_t kMaxLedCount = 300;

// ---- family-groups ----
constexpr size_t kMaxGroups = 32;              // keeps the device-config payload within 4 KB

// ---- firmware-updates ----
constexpr uint32_t kHealthyUptimeMs = 60000;               // provisional firmware is healthy after 60 s
constexpr uint32_t kBrokerGraceAfterWifiMs = 10UL * 60000; // ... and a broker connection within 10 min of Wi-Fi
constexpr uint8_t kMaxProvisionalBoots = 3;                // revert after 3 unhealthy boots
constexpr size_t kRolledBackListSize = 4;                  // remember the last 4 rolled-back versions

// ---- device-provisioning ----
constexpr uint32_t kBackoffCapMs = 60000;

}  // namespace limits
}  // namespace yule
