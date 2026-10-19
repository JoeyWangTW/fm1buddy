// A USB stick on the hub, for the ultra-light build (no drive of its own): its /midimap.txt is read when it is
// plugged in. Read on core 1 (where the USB host is), handed to core 0 as text.
#pragma once

#include <Arduino.h>

#define STICK_TEXT 8192  // the longest midimap.txt read (the rest is left out)

void stickTask();  // core 1, from hostLoop()
// core 0: midimap.txt from a stick that was just plugged in, or 0. stickDone() once it has been read
const char* stickText();
void stickDone();
