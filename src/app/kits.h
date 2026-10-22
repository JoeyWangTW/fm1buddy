// fomajam's kits (its src/drums.c, and its page's web/fm1bank.js): folders of .wav files made into the bank its store
// holds, its own drum machines after MIDI. Up to 4 kits, each named after its folder. A folder of files named after
// drum voices (bd.wav, sd.wav, ch.wav ...) is a drum kit; any other plays one file a key (27 at most).
//
//   "FJB1", kits (1 byte), entries (1 byte), 0
//   kits x 12 bytes: name (8, A-Z 0-9, 0 after), first entry, entries, 1 if a drum kit, 0
//   entries x 20 bytes: name (8), offset u32 (from the bank's start), len u32 (samples), rate u16 (Hz),
//     idx0 (the IMA step index it starts at), its voice in a drum kit (an index in VOICES)
//   the samples: IMA ADPCM, 4 bits each, the first in the low nibble
//
// Each .wav becomes 22050 Hz mono, its silent tail cut, at the level fomajam's own recordings of that voice have
// (one a key: -3 dB).
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#define KITS_MAX 4
#define KIT_SAMPLES 27

namespace kits {
struct Kit {
  std::string name;                  // "BOOMKIT"
  bool drum;                         // a drum kit
  std::vector<std::string> samples;  // their names ("BD", "CH"; "KICK")
};
// a folder's kits, by their names: each folder in it a kit (or, with .wav files of its own, the folder is one);
// true if it has any
bool scan(const std::string& dir, std::vector<Kit>& out);
// the bank from them; 0 or an error
const char* build(const std::string& dir, std::vector<uint8_t>& bank);
// a bank's kits; false if it isn't one
bool describe(const std::vector<uint8_t>& bank, std::vector<Kit>& out);
// "BOOMKIT (a drum kit), LOOPS (2 samples)"
std::string summary(const std::vector<Kit>& k);
// the name shown: a file's (or a folder's), in capitals, without leading numbers, up to max characters
std::string label(const std::string& file, int max);
}  // namespace kits
