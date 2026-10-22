// The FM-1's boot loader: it's in the chip's ROM (JieLi WL82), so no firmware can break it. A running FM-1 goes there
// when it's sent F0 22 24 35 7D F7 (stock V15, and the firmwares on Felucca's platform: fomajam ...), and it's then a
// USB storage device (4C4A:8057, "WL80UBOOT1.00") answering JieLi's own commands, until it's told to start its
// firmware again. With JieLi's loader in its RAM (wl82loader.bin, from jl-uboot-tool: downloaded once, to the SD
// card) it reads the whole flash: the firmware, and what the firmwares keep (settings, projects, samples, voices).
//
// The protocol is as FM-1-transporter (kurogedelic, MIT) and jl-uboot-tool (kagaimiq, MIT) speak it: their
// docs/PROTOCOL.md says how. Only reading here: nothing on the FM-1 is written.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "catalog.h"

#define UBOOT_LOADER DATA_DIR "/wl82loader.bin"
#define UBOOT_LOADER_URL                                                                                    \
  "https://raw.githubusercontent.com/kagaimiq/jl-uboot-tool/c32b28e7a04f873f526ba929cb1bae9f05442fe9/data/" \
  "loaderblobs/usb/wl82loader.bin"
#define UBOOT_LOADER_SHA256 "d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376"
#define BACKUPS_DIR "/backups"

namespace uboot {
// the loader is on the SD card (and is the one it should be)
bool haveLoader();

struct Chip {
  uint16_t key;      // its chip key (0x980F)
  uint32_t flashId;  // its flash's JEDEC id (0x856014: 1 MiB)
  uint32_t size;     // the flash's size
};
// device d (an FM-1, alone on the hub) into its boot loader, and the loader running in it; 0 or an error
const char* enter(int d, Chip& chip);
// the whole flash; 0 or an error
const char* read(const Chip& chip, std::vector<uint8_t>& out, void (*progress)(uint32_t done, uint32_t total));
// back to its firmware (it restarts)
void leave();
// it's still in its boot loader (after leave(): only switching it off and on gets it out)
bool stuck();
}  // namespace uboot
