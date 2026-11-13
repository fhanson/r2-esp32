#pragma once
// Builds command strings for the Human Cyborg Relations (HCR) Vocalizer.
//
// Syntax is from docs.humancyborgrelations.com (embedded > Commands) and the
// HumanCyborgRelationsAPI library the Mega used: every command sits in a
// <...> container, and one container can hold several commands separated by
// commas. The strings here have no line ending; HcrLink adds it.
//
// Each function writes a NUL-terminated string to `out` and returns its
// length, or 0 if the arguments are invalid or it doesn't fit in `cap`.

#include <stddef.h>
#include <stdint.h>

namespace droid {

constexpr uint16_t kHcrMaxWav = 9999;
constexpr uint8_t kHcrMaxLevel = 100;

// Values sent once at startup. Levels above 100 are clamped.
struct HcrSetup {
  uint8_t vocalizerVolume = 100;  // the vocalizer's own voice (PVV)
  uint8_t wavAVolume = 80;        // WAV channel A (PVA)
  uint8_t wavBVolume = 80;        // WAV channel B (PVB)
  uint8_t happy = 0;
  uint8_t sad = 0;
  uint8_t mad = 0;
  uint8_t scared = 0;
};

// <CA0021>: play one WAV file. channel is 'A' or 'B'.
size_t hcrPlayWav(char* out, size_t cap, char channel, uint16_t file);

// <CA0009C0052>: play a random WAV file from first to last.
size_t hcrPlayRandomWav(char* out, size_t cap, char channel, uint16_t first, uint16_t last);

// <SH0>: stimulate an emotion. emotion 0 happy, 1 sad, 2 mad, 3 scared;
// strong false is HCR's "moderate". Same as the library's Stimulate().
size_t hcrStimulate(char* out, size_t cap, uint8_t emotion, bool strong);

// <O1,OH89,OS22,OM0,OC0,PVV100,PVA80,PVB80>: emotion override on, emotions
// and all three volumes set. Each volume is separate in HCR (0-100) and HCR
// saves it to Config.txt on its SD card, so send these only at startup and
// when a setting changes. There's no master volume; the amp knob is that.
size_t hcrStartup(char* out, size_t cap, const HcrSetup& setup);

}  // namespace droid
