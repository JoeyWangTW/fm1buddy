// The MIDI mapping rules (common/fm1map.h): kept as text in /midimap.txt on the SD card, one rule per line,
// and sent to the host box with the device names turned into its devices
#pragma once

#include <Arduino.h>
#include <FS.h>

#include "fm1map.h"

#define MAP_FILE "/midimap.txt"

struct MapRule {
  char from[24];  // part of a device name, lowercase; "": any
  char to[24];    // "": where the routing sends it
  uint8_t inType, inCh, inNum;
  uint8_t outType, outCh, outNum;  // outType MAP_DROP: drop it
};

namespace midimap {
extern MapRule rules[MAP_RULES];
extern int count;

// "from nano cc 10 -> ch 2 cc 5": false (and why) when it doesn't read; blank lines and comments are false
// with why = 0
bool parse(const char* line, MapRule& r, const char** why);
void format(const MapRule& r, char* out, size_t n);
bool load(fs::FS& fs);
bool save(fs::FS& fs);
// to the box, for the devices that are on its hub now
void push();
}  // namespace midimap
