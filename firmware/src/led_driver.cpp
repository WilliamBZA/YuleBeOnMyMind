#include "led_driver.h"

void LedDriver::begin() {
  FastLED.addLeds<WS2812B, YULE_PIN_LED, YULE_LED_COLOR_ORDER>(leds_, YULE_LED_COUNT);
  // The compositor applies the owner's brightness, so FastLED stays at full scale. The power limit scales
  // the whole frame down when the strip would draw more than the budget (design D8, default 450 mA).
  FastLED.setBrightness(255);
  FastLED.setMaxPowerInVoltsAndMilliamps(YULE_LED_VOLTS, YULE_LED_MAX_MA);
  FastLED.clear(true);
}

void LedDriver::show(const yule::Rgb* pixels, size_t n) {
  const size_t used = n < count() ? n : count();
  for (size_t i = 0; i < used; ++i) leds_[i].setRGB(pixels[i].r, pixels[i].g, pixels[i].b);
  for (size_t i = used; i < count(); ++i) leds_[i] = CRGB::Black;
  FastLED.show();
}

void LedDriver::sweep() {
  for (size_t i = 0; i < count(); ++i) {
    FastLED.clear();
    leds_[i] = CRGB(60, 60, 60);
    FastLED.show();
    delay(50);
  }
  FastLED.clear(true);
}
