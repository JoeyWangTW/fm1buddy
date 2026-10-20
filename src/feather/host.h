// The box on the RP2040: the USB host on core 1 (on the Feather's USB-A socket, or the board's own USB port), with
// the routing and the mapping (box.h). Everything else (this board's screen, a CYD on the UART) talks to it with link
// frames (fm1link.h) through two queues between the cores.
#pragma once

#include <Arduino.h>

#include "box.h"
#include "pico/util/queue.h"

extern queue_t toHost, fromHost;

void hostQueues();  // core 0, before core 1 starts
void hostSetup();   // core 1
void hostLoop();    // core 1, over and over
