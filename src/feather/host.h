// The box itself, on core 1: the USB host (on the Feather's USB-A socket), the routing and the mapping. Whatever
// any device on the hub sends goes to all of the others, unless the routing or a mapping rule says otherwise.
// Everything else (this board's screen, a CYD on the UART) talks to it with link frames (fm1link.h) through two
// queues between the cores.
#pragma once

#include <Arduino.h>

#include "fm1link.h"
#include "pico/util/queue.h"

struct HostFrame {
  uint8_t type, dev;
  uint16_t n;
  uint8_t p[LINK_PAYLOAD];
};

extern queue_t toHost, fromHost;

void hostQueues();  // core 0, before core 1 starts
void hostSetup();   // core 1
void hostLoop();    // core 1, over and over
