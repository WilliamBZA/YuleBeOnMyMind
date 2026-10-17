#include "rtttl.h"

#include <cmath>

namespace yule {

uint32_t Melody::totalMs() const {
  uint32_t total = 0;
  for (const Note& n : notes) total += n.durMs;
  return total;
}

uint16_t noteFrequencyHz(int octave, int semitone) {
  const int midi = 12 * (octave + 1) + semitone;
  const double hz = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
  return static_cast<uint16_t>(hz + 0.5);
}

namespace {

constexpr int kMinOctave = 3;
constexpr int kMaxOctave = 8;
constexpr int kMinTempo = 10;
constexpr int kMaxTempo = 900;

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool validDuration(int d) { return d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 32; }

std::string quoted(char c) {
  if (c >= 0x20 && c < 0x7f) return std::string("'") + c + "'";
  return "that character";
}

class Parser {
 public:
  explicit Parser(std::string_view text) : t_(text) {}

  RtttlResult run() {
    const size_t c1 = t_.find(':');
    if (c1 == std::string_view::npos) {
      fail(t_.size(), "Missing ':' after the tune name");
      return result_;
    }
    const size_t c2 = t_.find(':', c1 + 1);
    if (c2 == std::string_view::npos) {
      fail(t_.size(), "Missing the second ':' before the notes");
      return result_;
    }
    result_.melody.name = trimmed(t_.substr(0, c1));
    if (!parseDefaults(c1 + 1, c2)) return result_;
    if (!parseNotes(c2 + 1, t_.size())) return result_;
    result_.ok = true;
    return result_;
  }

 private:
  bool fail(size_t index, const std::string& message) {
    result_.ok = false;
    result_.error.position = index + 1;
    result_.error.message = message;
    return false;
  }

  static std::string trimmed(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && isSpace(s[a])) ++a;
    while (b > a && isSpace(s[b - 1])) --b;
    return std::string(s.substr(a, b - a));
  }

  void skipSpace(size_t& i, size_t end) const {
    while (i < end && isSpace(t_[i])) ++i;
  }

  // Reads up to maxDigits decimal digits. Returns false if there are none. `tooLong` is set when more follow.
  bool readNumber(size_t& i, size_t end, int& value, bool& tooLong) const {
    constexpr int kMaxDigits = 4;
    int digits = 0;
    value = 0;
    tooLong = false;
    while (i < end && isDigit(t_[i])) {
      if (digits < kMaxDigits) {
        value = value * 10 + (t_[i] - '0');
      } else {
        tooLong = true;
      }
      ++digits;
      ++i;
    }
    return digits > 0;
  }

  bool parseDefaults(size_t begin, size_t end) {
    size_t i = begin;
    for (;;) {
      skipSpace(i, end);
      if (i >= end) return true;
      if (t_[i] == ',') {
        ++i;
        continue;
      }
      const char key = lower(t_[i]);
      if (key != 'd' && key != 'o' && key != 'b') {
        return fail(i, "Unknown setting " + quoted(t_[i]) + " in the defaults (expected d, o or b)");
      }
      ++i;
      skipSpace(i, end);
      if (i >= end || t_[i] != '=') return fail(i, std::string("Expected '=' after '") + key + "'");
      ++i;
      skipSpace(i, end);
      const size_t valuePos = i;
      int value = 0;
      bool tooLong = false;
      if (!readNumber(i, end, value, tooLong)) return fail(i, std::string("Expected a number after '") + key + "='");
      if (tooLong) return fail(valuePos, "That number is too large");
      if (key == 'd') {
        if (!validDuration(value)) return fail(valuePos, "Invalid default duration (use 1, 2, 4, 8, 16 or 32)");
        defaultDuration_ = value;
      } else if (key == 'o') {
        if (value < kMinOctave || value > kMaxOctave) return fail(valuePos, "Invalid default octave (use 3 to 8)");
        defaultOctave_ = value;
      } else {
        if (value < kMinTempo || value > kMaxTempo) return fail(valuePos, "Invalid tempo (use 10 to 900 beats per minute)");
        tempo_ = value;
      }
      skipSpace(i, end);
      if (i < end && t_[i] != ',') {
        return fail(i, "Unexpected character " + quoted(t_[i]) + " in the defaults");
      }
    }
  }

  bool parseNotes(size_t begin, size_t end) {
    size_t i = begin;
    for (;;) {
      skipSpace(i, end);
      if (i >= end) break;
      if (t_[i] == ',') {
        ++i;
        continue;
      }
      if (!parseNote(i, end)) return false;
      skipSpace(i, end);
      if (i < end && t_[i] != ',') {
        return fail(i, "Unexpected character " + quoted(t_[i]) + " after a note");
      }
    }
    if (result_.melody.notes.empty()) return fail(begin, "The tune has no notes");
    return true;
  }

  // Grammar: [duration] letter [#] and then a dot and/or an octave in either order, at most one of each.
  bool parseNote(size_t& i, size_t end) {
    int duration = defaultDuration_;
    if (isDigit(t_[i])) {
      const size_t durationPos = i;
      bool tooLong = false;
      readNumber(i, end, duration, tooLong);
      if (tooLong || !validDuration(duration)) {
        return fail(durationPos, "Invalid note duration (use 1, 2, 4, 8, 16 or 32)");
      }
      skipSpace(i, end);
    }
    if (i >= end || t_[i] == ',') return fail(i, "A note is missing its letter (c d e f g a b or p)");

    int semitone = 0;
    bool pause = false;
    switch (lower(t_[i])) {
      case 'c': semitone = 0; break;
      case 'd': semitone = 2; break;
      case 'e': semitone = 4; break;
      case 'f': semitone = 5; break;
      case 'g': semitone = 7; break;
      case 'a': semitone = 9; break;
      case 'b': semitone = 11; break;
      case 'p': pause = true; break;
      default:
        return fail(i, "Invalid note " + quoted(t_[i]) + " (use c d e f g a b or p)");
    }
    ++i;

    bool sharp = false;
    if (i < end && t_[i] == '#') {
      if (pause) return fail(i, "A pause cannot be sharpened");
      sharp = true;
      ++i;
    }

    bool dotted = false;
    bool octaveSeen = false;
    int octave = defaultOctave_;
    for (;;) {
      if (i < end && t_[i] == '.') {
        if (dotted) return fail(i, "A note can have only one dot");
        dotted = true;
        ++i;
      } else if (i < end && isDigit(t_[i]) && !octaveSeen) {
        octave = t_[i] - '0';
        if (octave < kMinOctave || octave > kMaxOctave) return fail(i, "Invalid octave (use 3 to 8)");
        octaveSeen = true;
        ++i;
      } else {
        break;
      }
    }

    Note note;
    double ms = 240000.0 / static_cast<double>(tempo_) / static_cast<double>(duration);
    if (dotted) ms *= 1.5;
    note.durMs = static_cast<uint32_t>(ms + 0.5);
    if (note.durMs == 0) note.durMs = 1;
    if (pause) {
      note.freqHz = 0;
    } else {
      int sem = semitone + (sharp ? 1 : 0);
      int oct = octave;
      if (sem >= 12) {
        sem -= 12;
        ++oct;
      }
      note.freqHz = noteFrequencyHz(oct, sem);
    }
    result_.melody.notes.push_back(note);
    return true;
  }

  std::string_view t_;
  RtttlResult result_;
  int defaultDuration_ = kRtttlDefaultDuration;
  int defaultOctave_ = kRtttlDefaultOctave;
  int tempo_ = kRtttlDefaultTempo;
};

}  // namespace

RtttlResult parseRtttl(std::string_view text) { return Parser(text).run(); }

}  // namespace yule
