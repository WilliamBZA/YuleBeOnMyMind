#include "serial_console.h"

bool SerialConsole::poll(std::string& line) {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      const bool usable = !overflow_ && !buffer_.empty();
      overflow_ = false;
      if (usable) {
        line = buffer_;
        buffer_.clear();
        return true;
      }
      buffer_.clear();
      continue;
    }
    if (buffer_.size() >= kMaxLine) {
      overflow_ = true;
      continue;
    }
    buffer_.push_back(static_cast<char>(c));
  }
  return false;
}
