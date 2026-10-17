#include "song_library.h"

namespace yule {

namespace {

// Transcribed by hand as simplified melodies; tune them by ear on real hardware (README, "Songs").
const SongInfo kCarols[] = {
    {"jingle-bells", "Jingle Bells",
     "JingleBells:d=8,o=5,b=112:"
     "e,e,4e,e,e,4e,e,g,c.,16d,2e,"
     "f,f,f.,16f,f,e,e,16e,16e,e,d,d,e,4d,4g,"
     "e,e,4e,e,e,4e,e,g,c.,16d,2e,"
     "f,f,f.,16f,f,e,e,16e,16e,g,g,f,d,2c"},

    {"we-wish-you-a-merry-christmas", "We Wish You a Merry Christmas",
     "WeWish:d=4,o=5,b=140:"
     "d,g,8g,8a,8g,8f#,e,e,e,a,8a,8b,8a,8g,f#,d,d,"
     "b,8b,8c6,8b,8a,g,e,8d,8d,e,a,f#,2g."},

    {"deck-the-halls", "Deck the Halls",
     "DeckTheHalls:d=4,o=5,b=120:"
     "g.,8f,e,d,c,d,e,c,8d,8e,8f,8d,e.,8d,c,b4,2c,"
     "g.,8f,e,d,c,d,e,c,8d,8e,8f,8d,e.,8d,c,b4,2c"},

    {"silent-night", "Silent Night",
     "SilentNight:d=4,o=5,b=80:"
     "g.,8a,g,2e.,g.,8a,g,2e.,"
     "2d6,d6,f6.,8d6,b,2c6.,2e6.,c6,g,e,g.,8f,d,2c."},

    {"joy-to-the-world", "Joy to the World",
     "JoyToTheWorld:d=4,o=6,b=120:"
     "c,8b5.,16a5,g5.,8f5,e5,d5,2c5,p,"
     "c,8b5.,16a5,g5.,8f5,e5,d5,2c5"},

    {"o-come-all-ye-faithful", "O Come, All Ye Faithful",
     "Adeste:d=4,o=5,b=100:"
     "g,g,d,g,a,2d,d,g,a,g,2f#,2g,"
     "p,a,a,b,a,g,2f#,e,f#,2g."},

    {"what-child-is-this", "What Child Is This (Greensleeves)",
     "Greensleeves:d=4,o=5,b=132:"
     "a,2c6,d6,e6.,8f6,e6,2d6,b,g.,8a,b,2c6,a,a.,8g#,a,2b,g#,2e.,"
     "a,2c6,d6,e6.,8f6,e6,2d6,b,g.,8a,b,c6.,8b,a,g#.,8f#,g#,2a."},

    {"the-first-noel", "The First Noel",
     "FirstNoel:d=4,o=5,b=100:"
     "e,d.,8c,d,e,f,2g,a,g,f,e,d,2c"},
};

constexpr size_t kCarolCount = sizeof(kCarols) / sizeof(kCarols[0]);

}  // namespace

size_t carolCount() { return kCarolCount; }

const SongInfo& carolAt(size_t index) { return kCarols[index < kCarolCount ? index : 0]; }

const SongInfo* findCarol(std::string_view id) {
  for (const SongInfo& s : kCarols) {
    if (id == s.id) return &s;
  }
  return nullptr;
}

const SongInfo& defaultCarol() { return kCarols[0]; }

}  // namespace yule
