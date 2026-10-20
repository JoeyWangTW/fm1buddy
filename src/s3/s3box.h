// The box on the ESP32-S3 (the cyd-s3 build): its own USB-C port is the USB host, through a USB-C OTG adapter with
// a charging port, to a hub. The ESP-IDF USB host (with its hub driver) and a small USB MIDI driver carry box.cpp,
// which runs in a task of its own; the screen's link (hub, link.h) reaches it through two queues.
//
// The S3's USB has 8 channels: the hub takes 2, and each device 3 (its control endpoint, MIDI in, MIDI out). So
// the FM-1 and one controller on the hub; a third device doesn't fit.
#pragma once

#include <Arduino.h>

namespace s3box {
void begin();
// the link's two ends (hub.begin): a frame to the box, and what the box said, handed to hub.frame()
void send(uint8_t type, uint8_t d, const uint8_t* p, uint32_t n);
void pump();
}  // namespace s3box
