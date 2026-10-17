// The push button: a GPIO with the internal pull-up, closing to ground. Debouncing and press length are
// handled by the core's ButtonGestures; this only reads the pin.
#pragma once

#include <Arduino.h>

#include "button_gestures.h"
#include "yule_config.h"

class ButtonDriver {
 public:
  void begin() { pinMode(YULE_PIN_BUTTON, INPUT_PULLUP); }

  // True while the button is physically held down (the input reads low).
  bool pressed() const { return digitalRead(YULE_PIN_BUTTON) == LOW; }

  // Samples the pin and feeds the recogniser. Call every loop tick.
  void update(yule::ButtonGestures& gestures, uint32_t nowMs) const { gestures.update(nowMs, pressed()); }
};
