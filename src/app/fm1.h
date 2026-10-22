// The FM-1's USB-MIDI update protocol (M-UPGRADE compatible), on midi.h. A port of foma's web/fm1ota.js and
// web/fm1pkg.js (Leo Kuroshita, Hugelton Instruments; GPL-3.0-only), by way of src/common/fm1ota.cpp.
//
// Step 1: the running firmware reads parts of the package and starts the update loader. Step 2: the loader (a new
// USB device, "ota-...") reads the whole image and writes it. In both, the device sends cmd 0x30 read requests on
// the logical image (the .fwsc without its 20 marker bytes) and we answer them straight from the SD card.
#pragma once

#include <stdint.h>
#include <stdio.h>

#include <atomic>

// who a device says it is ("FM-1_904", "ota-FM-1_900")
struct Fm1Id {
  char text[28];
  char model[24];  // "FM-1", "ota-FM-1"
  uint32_t version;
  bool ota() const;
};

// a .fwsc on the SD card
struct Pkg {
  FILE* f = 0;
  uint32_t size = 0;      // of the logical image
  char product[24] = "";  // "FM-1_904"
  bool open(const char* path);
  void close();
  bool read(uint32_t addr, uint8_t* out, uint32_t len);
  bool fits(const Fm1Id& id) const;  // for this device's model ("FM-1_904" on "FM-1_15")
  ~Pkg() { close(); }
};

enum Fm1Step { STEP_START,
               STEP_VERIFY,
               STEP_LOADER,
               STEP_WRITE,
               STEP_REBOOT,
               STEP_DONE };

typedef void (*Fm1Progress)(Fm1Step step, uint32_t served, uint32_t addr);

namespace fm1 {
// a full install: running firmware -> loader -> new firmware; or, with the FM-1 already in update mode (the loader),
// the rest of one. Blocks (a job's thread). Returns an error, or 0
const char* install(Pkg& pkg, Fm1Progress cb);
// call often (not while installing): asks each device that may be an FM-1 who it is, once it has settled
void watch();
// what device d said it is (an FM-1, or its loader), or 0
const Fm1Id* known(int d);
// the first device that answered, -1 if none
int current();
extern std::atomic<bool> busy;  // installing
// the package being installed (for the simulator's pretend FM-1)
extern std::atomic<uint32_t> imageSize;
extern char imageProduct[24];
}  // namespace fm1
