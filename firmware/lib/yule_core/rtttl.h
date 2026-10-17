// RTTTL ring-tone parser: "name:defaults:notes", e.g. "Jingle:d=8,o=5,b=112:e,e,4e,e,e,4e,e,g,c.,16d,2e".
// Hardware independent. See the melody-playback spec for the accepted grammar.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace yule {

struct Note {
  uint16_t freqHz = 0;   // 0 means a rest
  uint32_t durMs = 0;
};

struct Melody {
  std::string name;
  std::vector<Note> notes;

  uint32_t totalMs() const;
};

struct RtttlError {
  size_t position = 0;   // 1-based character position of the first problem in the input
  std::string message;
};

struct RtttlResult {
  bool ok = false;
  Melody melody;
  RtttlError error;
};

// RTTTL defaults applied when the defaults section omits a value.
constexpr int kRtttlDefaultDuration = 4;
constexpr int kRtttlDefaultOctave = 6;
constexpr int kRtttlDefaultTempo = 63;

// Parses a tune. Accepts whitespace and upper-case letters, a dot before or after the octave, octaves 3-8,
// durations 1, 2, 4, 8, 16, 32 and tempos 10-900. Never throws and never reads outside `text`.
RtttlResult parseRtttl(std::string_view text);

// Equal-tempered frequency (A4 = 440 Hz) for a note, rounded to whole hertz. semitone: C = 0 ... B = 11.
uint16_t noteFrequencyHz(int octave, int semitone);

}  // namespace yule
