#include "log.h"

#include <Arduino.h>

void yuleLog(const char* tag, const char* fmt, ...) {
  char message[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);

  const unsigned long ms = millis();
  Serial.printf("[%5lu.%03lu] [%s] %s\n", ms / 1000UL, ms % 1000UL, tag, message);
}
