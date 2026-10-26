#include "buzzer_driver.h"

#include "log.h"
#include "yule_config.h"

namespace {

// 10 bits of duty resolution is plenty for a square wave and allows tones up to about 78 kHz.
constexpr uint8_t kResolutionBits = 10;
constexpr uint32_t kStartFrequencyHz = 1000;

}  // namespace

void BuzzerDriver::begin() {
  // Arduino-ESP32 3.x LEDC API: the channel is picked automatically and addressed by pin.
  attached_ = ledcAttach(YULE_PIN_BUZZER, kStartFrequencyHz, kResolutionBits);
  if (!attached_) {
    yuleLog("sound", "could not attach the buzzer to GPIO %d; playing silently", YULE_PIN_BUZZER);
    return;
  }
  ledcWrite(YULE_PIN_BUZZER, 0);
  current_ = 0;
}

void BuzzerDriver::play(uint16_t hz) {
  if (!attached_ || hz == current_) return;
  current_ = hz;
  // ledcWriteTone sets 50% duty at the new frequency; 0 Hz sets the duty to 0, holding the pin low so the
  // piezo carries no DC.
  ledcWriteTone(YULE_PIN_BUZZER, hz);
}
