// MIDI between the devices on the hub: whatever any of them sends goes to all the others ("thru"), except while the
// FM-1 is being updated. The board's USB host task calls mount, umount, in and service; the rest is safe from any
// thread.
#pragma once

#include <stdint.h>

#include <atomic>

#define MIDI_USB 4    // devices 0-3: USB, on the hub
#define MIDI_DEVS 8   // 4-7: Bluetooth
#define SYSEX_MAX 96  // the longest SysEx kept (the FM-1's answers are 19 and 41 bytes on the wire)

namespace midi {
struct Dev {
  bool on;
  uint16_t vid, pid;
  char name[32];
  uint32_t serial;  // new on every mount: a new device in the same slot is noticed
  uint32_t lastIn;  // when it last sent something (ms)
  uint8_t last[3];  // the last channel message it sent
  uint32_t count;   // messages it sent
};

extern std::atomic<bool> thru;         // everything to everything
extern std::atomic<uint32_t> seq;      // goes up when a device comes or goes
extern std::atomic<uint32_t> dropped;  // messages that didn't fit a device's queue (it doesn't take them fast enough)

// a copy of device d: false if nothing is there
bool dev(int d, Dev& out);

// the USB host task
void mount(int d, uint16_t vid, uint16_t pid, const char* name);
void umount(int d);
void in(int d, const uint8_t p[4]);  // a USB MIDI packet it sent
// hands what's waiting for devices from..to-1 to them (hal::midiOut): the USB task does the USB ones, the
// Bluetooth loop the others
void service(int from = 0, int to = MIDI_USB);

// MIDI bytes a device sent (Bluetooth)
void bytesIn(int d, const uint8_t* bytes, uint32_t n);

// anywhere
void send(int d, const uint8_t* bytes, uint32_t n);  // MIDI bytes to device d
uint32_t sysex(int d, uint8_t* out);                 // the next complete SysEx (F0..F7) it sent, or 0
void drain(int d);                                   // forget the SysEx it sent

// "note 60 ch 1", "cc 10 = 64 ch 2", ...
void describe(const uint8_t m[3], char* out, int n);
}  // namespace midi
