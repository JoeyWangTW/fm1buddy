// The box: the routing and the mapping between the USB MIDI devices on the hub and the screen's own (Bluetooth),
// and the link frames (fm1link.h) to whoever listens. The same on every board: the board has the USB host and
// carries the frames (the usb* and box* functions below, the board's).
#pragma once

#include <Arduino.h>

#include "fm1link.h"

struct HostFrame {
  uint8_t type, dev;
  uint16_t n;
  uint8_t p[LINK_PAYLOAD];
};

namespace box {
void begin();  // says HELLO
// call over and over, after the USB host's own task
void loop();
// device i on the hub is ready: its product string descriptor (UTF-16LE, as it came; 0 or empty: none)
void mount(uint8_t i, uint16_t vid, uint16_t pid, const uint8_t* desc);
void umount(uint8_t i);  // it went
}  // namespace box

// ---- the board's
// device i (0..LINK_USB-1) is on the hub (mounted, named or not)
bool usbMounted(uint8_t i);
// the next USB MIDI packet (4 bytes) it sent
bool usbRead(uint8_t i, uint8_t p[4]);
// one packet to it
void usbWrite(uint8_t i, const uint8_t p[4]);
// MIDI bytes to it (cable 0): how many were taken (the rest waits for room)
uint32_t usbStreamWrite(uint8_t i, const uint8_t* p, uint32_t n);
// what was written goes out now
void usbFlush(uint8_t i);
// a frame for the screen. MIDI (LINK_MIDI, LINK_SEND) may be dropped when there is no room
void boxOut(uint8_t type, uint8_t dev, const uint8_t* p, uint32_t n);
// the next frame from the screen, if there is one
bool boxIn(HostFrame& f);
// MIDI passed through (the activity light)
void boxActivity();
