// The "thinking of you" press message (design D4):
//   {"v":1,"id":"9f3c2a17b04e6d58","from":"d-3c71bf4a2b10","name":"Gran","ts":1797000000,
//    "song":"Jingle:d=8,o=5,b=112:...","pattern":{"effect":"twinkle","colors":["#C8102E"],"speed":2}}
// Receivers ignore unknown fields and tolerate newer firmware. Hardware independent.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "pattern.h"

namespace yule {

struct PressMessage {
  int version = 1;
  std::string id;    // unique per press; the same id is used for every group copy
  std::string from;  // sender device id
  std::string name;  // sender display name
  int64_t ts = 0;    // send time, UTC epoch seconds; 0 when the sender's clock was not synchronised
  std::string song;  // RTTTL
  Pattern pattern;
};

// A message id is 1-32 characters from [0-9A-Za-z].
bool isValidMessageId(std::string_view id);

// Serialises a message. `ts` is omitted when it is 0.
std::string encodePress(const PressMessage& message);

enum class PressDecodeStatus {
  Ok,
  Malformed,    // not JSON, not an object, or too large
  MissingFrom,  // no valid sender device id
  MissingId,    // no valid message id, so it cannot be de-duplicated
};

struct PressDecodeResult {
  PressDecodeStatus status = PressDecodeStatus::Malformed;
  PressMessage message;  // normalised when status is Ok
};

// Decodes and normalises a received payload:
//  - an unknown effect becomes "breathe" with the supplied colours
//  - missing or invalid colours become warm white
//  - a missing, invalid or over-long (> 512 characters) song becomes the default chime
// Never crashes or hangs on any input.
PressDecodeResult decodePress(std::string_view payload);

}  // namespace yule
