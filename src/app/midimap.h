// MIDI mapping: rules for what MIDI routing (midi.h) does to each channel message on its way between devices. They're
// kept as text in /midimap.txt on the SD card, one rule per line (the file explains itself: HELP in midimap.cpp):
//
//   [from NAME] [ch N] [TYPE [NUM[-NUM]]] -> [to NAME] [ch N] [TYPE [NUM]] [+N|-N] [vel N|vel N%]
//   [from NAME] [ch N] [TYPE [NUM[-NUM]]] -> drop
//
// TYPE is note, cc, pc (program change), at (channel pressure) or pb (pitch bend); NAME is part of a device's name (any
// case). On the left, NUM-NUM is a range (note 0-59: the keys below middle C). On the right, +N or -N moves notes by
// that many semitones, vel N plays every note at that velocity and vel N% scales it. The first rule that matches a message decides; a message no rule matches goes to all the other devices, as
// without a file. On the right, what's left out stays as it came in; without "to", it goes where it would have. A
// message's value (a note's velocity, a CC's value, the program, the pressure, the bend's top 7 bits) carries over to
// what it becomes: a CC turned into a note plays it at the CC's value (0 is note off), a CC turned into a program
// change picks the program from the value (unless the rule names one).
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#define MAP_FILE "/midimap.txt"
#define MAP_RULES 64
#define MAP_ANY 0xFF   // inNum: any number; outNum: the same
#define MAP_DROP 0xFF  // outType

enum { MAP_NOTE = 0x9,
       MAP_POLY = 0xA,
       MAP_CC = 0xB,
       MAP_PC = 0xC,
       MAP_AT = 0xD,
       MAP_PB = 0xE };

struct MapRule {
  char from[24] = "";                                             // part of a device's name, lowercase; "": any
  char to[24] = "";                                               // "": where routing sends it
  uint8_t inType = 0, inCh = 0, inNum = MAP_ANY, inHi = MAP_ANY;  // inNum..inHi: a range (the same: one number)
  uint8_t outType = 0, outCh = 0, outNum = MAP_ANY;
  int8_t transpose = 0;  // semitones, for notes
  uint8_t vel = 0;       // a note on's velocity: 0 as it came, else this
  uint16_t velPct = 0;   // or scaled: 0 as it came, else percent (1-400)
};

namespace midimap {
// a line: false when it isn't a rule; why says what's wrong with it (0 for a blank line or a comment)
bool parse(const char* line, MapRule& r, const char** why);
// the rules in path (MAP_FILE on the SD card): false if it isn't there. With an error, problem says which line and why
bool load(const char* path, std::vector<MapRule>& out, std::string& problem);
// a file that only explains itself (no rules: everything as without one)
bool writeHelp(const char* path);
// a rule as a line of the file (parse reads it back the same)
std::string text(const MapRule& r);
// in plain words, for the screen: "nanokey, notes 0-59 -> fm-1 ch 2, +12"
std::string describe(const MapRule& r);
// does device name (as it says it) match a rule's from or to? ("" matches all)
bool matches(const char* part, const char* name);

// msg: a channel message (status 0x80-0xEF) from device d. from[i] and to[i]: rule i's devices (bit n: device n).
// Returns the rule that matched, -1 if none (it goes on as it is). When one did: *outN is 0 to drop it, or out holds
// what it became, and *toMask the devices it goes to (0: where routing sends it)
int apply(const std::vector<MapRule>& rules, const uint32_t* from, const uint32_t* to, int d, const uint8_t* msg,
          uint8_t* out, uint32_t* outN, uint32_t* toMask);
}  // namespace midimap
