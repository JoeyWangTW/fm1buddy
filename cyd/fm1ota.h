// The FM-1 USB-MIDI update protocol (M-UPGRADE compatible), through the host box. A port of foma's
// web/fm1ota.js and web/fm1pkg.js (Leo Kuroshita, Hügelton Instruments; GPL-3.0-only).
//
// Step 1: the running firmware reads parts of the package and stages the update loader. Step 2: the loader
// (a new USB device, "ota-...") reads the whole image and writes it. In both steps the device sends cmd 0x30
// read requests on the logical image and we answer them, straight from the SD card.
#pragma once

#include <Arduino.h>
#include <FS.h>

// the identity a device answers the handshake with ("FM-1_904", "ota-FM-1_900")
struct Fm1Id {
  char text[28];
  char model[24];
  uint32_t version;
  bool ota() const { return strncasecmp(model, "ota-", 4) == 0; }
};

// a .fwsc on the SD card
struct Pkg {
  File f;
  uint32_t size;     // of the logical image (the file without its 20 marker bytes)
  char product[24];  // "FM-1_904"
  bool open(fs::FS& fs, const char* path);
  bool read(uint32_t addr, uint8_t* out, uint32_t len);
  // the package is for this device's model ("FM-1_904" on "FM-1_15")
  bool fits(const Fm1Id& id) const;
};

enum Fm1Step { STEP_START,
               STEP_VERIFY,
               STEP_LOADER,
               STEP_WRITE,
               STEP_REBOOT,
               STEP_DONE };

typedef void (*Fm1Progress)(Fm1Step step, uint32_t served, uint32_t addr);

namespace fm1 {
// the device can be an FM-1: by its USB name (never probe other gear)
bool candidate(uint8_t d);
// ask device d who it is
bool handshake(uint8_t d, Fm1Id& id, int tries = 2);
// the first FM-1 (ota: the loader, or the firmware), -1 if none
int find(bool ota, Fm1Id& id);
// full install: running firmware -> loader -> new firmware. Returns an error, or 0
const char* install(Pkg& pkg, Fm1Progress cb);
// the device is already in update mode (the loader): finish writing
const char* resume(Pkg& pkg, Fm1Progress cb);
// waits while keeping the link going
void wait(uint32_t ms);
}  // namespace fm1
