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
// rule i becomes r (i < 0: a new one at the end), or goes
void put(int i, const MapRule& r);
void remove(int i);

// editing a rule, the same on every screen. Its fields: 0 from, 1 channel, 2 type, 3 number (what comes in),
// 4 to, 5 channel, 6 type, 7 number (what it becomes)
void blank(MapRule& r);
void step(MapRule& r, int field, int dir);
// whether a field means something for this rule (the number of a pitch bend doesn't)
bool active(const MapRule& r, int field);
// the field's name ("FROM", "CHANNEL", "NOTE" ...)
const char* label(const MapRule& r, int field);
// what a field shows ("any", "same", "cc", "10", a device name)
const char* show(const MapRule& r, int field, char* buf, size_t n);
// the newest message that came in (hub.last) becomes the rule's left side
void learn(MapRule& r);
const char* typeName(uint8_t t);
bool hasNum(uint8_t t);  // the type has a number a rule can name (a note's, a CC's, a program)
}  // namespace midimap
