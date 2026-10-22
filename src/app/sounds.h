// Sounds: SysEx to and from the FM-1 (or any device on the hub), kept on the SD card as .syx files in /syx.
//
// Setups: a firmware that shares (fomajam, through foma's platform/src/share.c) gives its song (or whatever it keeps)
// as one block. It's kept on the SD card in a setup, a folder in /setups (setup-1/fomajam.bin), and loaded back later:
// one setup per song played, say.
//
// Any firmware: a .syx file is sent as it is, and whatever the device sends is recorded into one (most firmwares that
// can send their sounds do it from a menu on the device). On top of that, what the device's firmware speaks is asked
// (probe): the stock firmware's voice read, Felucca's editor protocol, M-VAVE Open's; each gets more later.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#define SYX_DIR "/syx"
#define SETUPS_DIR "/setups"

namespace sounds {
// what a device answered to
struct Speaks {
  bool stockVoice;   // M-VAVE's read (0x23): its current voice
  bool felucca;      // Felucca's editor protocol (F0 7D 46 4C)
  bool open;         // M-VAVE Open's file protocol (F0 7D 46 4D 02)
  char version[32];  // what Felucca said it is ("FELUCCA v1.6.1")
  bool share;        // it shares a block (a foma firmware: fomajam's song)
  uint32_t shareSize;
  char app[17], appVersion[9];  // the firmware that shares ("FOMAJAM", "0.1")
};

// a .syx: what's in it
struct Info {
  std::string kind;                // "DX7 bank: 32 voices", "DX7 voice", "3 SysEx messages"
  int messages = 0;                // complete F0..F7
  bool good = true;                // the DX7 checksums are right (and nothing outside F0..F7)
  std::vector<std::string> names;  // DX7 voice names
};
Info describe(const std::vector<uint8_t>& data);

struct File {
  std::string name;  // "bank.syx"
  uint32_t size;
};
// the .syx files on the SD card, by name
std::vector<File> list();
bool load(const std::string& name, std::vector<uint8_t>& out);
// as /syx/<prefix>-N.syx (the next N); "" if it didn't save
std::string save(const std::vector<uint8_t>& data, const char* prefix);

// the setups (folders in /setups), by name; and the file in one that firmware app's block is kept in ("" no SD card)
std::vector<std::string> setups();
std::string setupFile(const std::string& setup, const char* app);

// these block (a job's thread)
void probe(int d, Speaks& out);
// each message to device d, a moment apart; 0 or an error
const char* send(int d, const std::vector<uint8_t>& data, void (*progress)(uint32_t done, uint32_t total));
// the stock firmware's current voice, as a DX7 single-voice .syx; 0 or an error
const char* readVoice(int d, std::vector<uint8_t>& syx);
// the block device d's firmware shares, into a new setup (setup-N, its name in setup); 0 or an error
const char* keep(int d, std::string& setup, void (*progress)(uint32_t done, uint32_t total));
// a setup's block back to device d's firmware (which saves it); 0 or an error
const char* restore(int d, const std::string& setup, void (*progress)(uint32_t done, uint32_t total));
}  // namespace sounds
