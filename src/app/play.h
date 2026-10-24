// Playing tools on top of MIDI routing (midi.h): a MIDI clock (fm1buddy leads at its own tempo, or follows the clock
// a device sends), tap tempo, start and stop, and an arpeggiator and chord memory for the notes controllers play.
//
// The arpeggiator and chords take the notes of every device except the FM-1s (their own sequencers play as they
// are). What they play goes on as if the controller had played it: through the mapping, to the other devices. All of
// it runs only while MIDI routing is on.
#pragma once

#include <stdint.h>

namespace play {
enum Clock { CLOCK_OFF,     // clock goes between the devices as it is
             CLOCK_LEAD,    // fm1buddy sends clock at its tempo (and start/stop); the devices' own clock is dropped
             CLOCK_FOLLOW,  // the first device that sends clock sets the tempo
             CLOCKS };
enum Arp { ARP_UP,
           ARP_DOWN,
           ARP_UPDOWN,
           ARP_RANDOM,
           ARP_PLAYED,  // in the order they were played
           ARPS };
enum Rate { RATE_4,
            RATE_8,
            RATE_8T,
            RATE_16,
            RATE_16T,
            RATE_32,
            RATES };
enum Chord { CHORD_OFF,
             CHORD_MAJ,
             CHORD_MIN,
             CHORD_7,
             CHORD_MAJ7,
             CHORD_MIN7,
             CHORD_SUS4,
             CHORD_5,
             CHORD_OCT,
             CHORDS };

extern const char* const CLOCK_NAMES[CLOCKS];
extern const char* const ARP_NAMES[ARPS];
extern const char* const RATE_NAMES[RATES];
extern const char* const CHORD_NAMES[CHORDS];

// what's chosen, kept across restarts
struct Setup {
  uint8_t version;
  uint8_t clock;  // Clock
  uint16_t bpm;   // 30-300: leading, and the arpeggiator's without clock to follow
  bool arp;
  uint8_t arpMode;  // Arp
  uint8_t rate;     // Rate
  uint8_t octaves;  // 1-4
  bool latch;       // the notes keep playing after the keys are let go (until the next ones)
  uint8_t chord;    // Chord
};

void begin();  // loads the setup, starts the clock's thread
Setup setup();
void set(const Setup& s);  // and saves it
// the tempo now: the setup's, or (following) the one heard; 0 if there's none to follow yet
float tempo();
bool playing();  // started (by start(), or the clock followed)
void start();    // leading: start, from the top
void stop();
void tap();       // a tap of the tempo (leading): two or more set it
uint32_t beat();  // counts quarter notes, for a light that flashes on each
// a one-line summary: "lead 120 BPM, arp up 1/16, maj chords"
void summary(char* out, int n);
// the devices whose notes are left alone (the FM-1s), from the UI
void skip(uint32_t mask);

// from midi.cpp, with its lock held. out(d, msg, n) sends a channel message on as if device d played it; out(-1, &b,
// 1) sends realtime byte b to every device
typedef void (*Out)(int d, const uint8_t* msg, uint32_t n);
bool noteLocked(int d, const uint8_t* msg, Out out);  // a note on/off from d: true if it was taken (arp, chords)
bool realtimeLocked(int d, uint8_t b, Out out);       // clock, start, stop from d: true to drop it
void tickLocked(Out out);                             // the clock's thread: a 24th of a beat at our tempo
void allOffLocked(Out out);                           // routing stops: let go of everything
}  // namespace play
