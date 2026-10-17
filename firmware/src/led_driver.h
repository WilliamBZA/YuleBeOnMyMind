// WS2812B-style LED strip output on FastLED (design D8). The compositor in the core produces the frames;
// this only copies them to the strip, within the configured power budget.
#pragma once

#include <FastLED.h>

#include "rgb.h"
#include "yule_config.h"

class LedDriver {
 public:
  // Configures the pin, count, colour order and power budget from yule_config.h.
  void begin();

  // Sends one frame. `n` pixels are copied (at most YULE_LED_COUNT); any remaining LEDs are set to black.
  void show(const yule::Rgb* pixels, size_t n);

  // Lights each LED in turn, then clears the strip. Blocks for about a second: for start-up and bring-up.
  void sweep();

  static constexpr size_t count() { return YULE_LED_COUNT; }

 private:
  CRGB leds_[YULE_LED_COUNT];
};
