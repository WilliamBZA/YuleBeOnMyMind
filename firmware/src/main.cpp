#include <Arduino.h>

#include "app.h"

// Created in setup() rather than as a global, so that its members (WiFiManager, the MQTT queue, ...) are
// built after the Arduino core and FreeRTOS are fully up.
static App* app = nullptr;

void setup() {
  app = new App();
  app->begin();
}

void loop() { app->loop(); }
