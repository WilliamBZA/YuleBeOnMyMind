// A line reader on the USB serial port, for bring-up and provisioning: `password <value>`, `status`, `help`
// (the commands themselves are handled by App). Anyone with a USB cable can already re-flash the device, so
// this opens nothing new.
#pragma once

#include <Arduino.h>

#include <string>

class SerialConsole {
 public:
  // Collects typed characters; returns true and fills `line` (without the line ending) when a complete,
  // non-empty line has been entered. Lines longer than the limit are discarded.
  bool poll(std::string& line);

 private:
  static constexpr size_t kMaxLine = 160;
  std::string buffer_;
  bool overflow_ = false;
};
