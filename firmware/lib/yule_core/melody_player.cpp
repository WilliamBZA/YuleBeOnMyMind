#include "melody_player.h"

#include <utility>

namespace yule {

uint32_t MelodyPlayer::articulationGapMs(uint32_t noteMs) {
  uint32_t gap = noteMs / 8;
  if (gap < 5) gap = 5;
  if (gap > 30) gap = 30;
  if (gap > noteMs / 2) gap = noteMs / 2;
  return gap;
}

void MelodyPlayer::start(Melody melody, uint32_t nowMs) {
  melody_ = std::move(melody);
  totalMs_ = melody_.totalMs();
  clippedMs_ = totalMs_ < limits::kMelodyMaxMs ? totalMs_ : limits::kMelodyMaxMs;
  startMs_ = nowMs;
  index_ = 0;
  noteStartOffsetMs_ = 0;
  playing_ = !melody_.notes.empty() && clippedMs_ > 0;
}

void MelodyPlayer::stop() { playing_ = false; }

uint16_t MelodyPlayer::update(uint32_t nowMs) {
  if (!playing_) return 0;
  const uint32_t elapsed = nowMs - startMs_;  // unsigned subtraction handles wrap-around
  if (elapsed >= clippedMs_) {
    playing_ = false;
    return 0;
  }
  while (index_ < melody_.notes.size() && elapsed >= noteStartOffsetMs_ + melody_.notes[index_].durMs) {
    noteStartOffsetMs_ += melody_.notes[index_].durMs;
    ++index_;
  }
  if (index_ >= melody_.notes.size()) {
    playing_ = false;
    return 0;
  }
  const Note& note = melody_.notes[index_];
  if (note.freqHz == 0) return 0;
  const uint32_t intoNote = elapsed - noteStartOffsetMs_;
  const uint32_t soundingMs = note.durMs - articulationGapMs(note.durMs);
  return intoNote < soundingMs ? note.freqHz : 0;
}

}  // namespace yule
