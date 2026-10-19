// fm1buddy ultra-light: a Waveshare RP2040-Zero. Its one USB-C port is the USB host (through a USB-C OTG adapter
// with a charging port, to a hub), so there is no USB drive and no screen of its own. The box is host.cpp (from
// the Feather build) on core 1; on core 0:
// - the link to a CYD on GP0 (TX) and GP1 (RX) (common/fm1link.h), passed through to the box
// - the MIDI mapping rules in midimap.txt on a USB stick on the hub (stick.h), unless a CYD sends its own
#include <Arduino.h>

#include "fm1link.h"
#include "host.h"
#include "link.h"
#include "midimap.h"
#include "stick.h"

static volatile bool hostReady;
static link_rx_t uartRx;
static bool cydSeen;  // a CYD talks to the box: it sets the routing and mapping (from its own SD card)

// the link (hub) goes to core 1 through the queue
static void queueSend(uint8_t type, uint8_t d, const uint8_t* p, uint32_t n) {
  static HostFrame f;
  f.type = type;
  f.dev = d;
  f.n = (uint16_t)(n > LINK_PAYLOAD ? LINK_PAYLOAD : n);
  memcpy(f.p, p, f.n);
  queue_add_blocking(&toHost, &f);
}

// what the box says goes to a CYD; what a CYD says goes to the box
static void pump() {
  static HostFrame f;
  uint8_t wire[LINK_WIRE];
  while (queue_try_remove(&fromHost, &f)) {
    hub.frame(f.type, f.dev, f.p, f.n);
    Serial1.write(wire, link_encode(f.type, f.dev, f.p, f.n, wire));
  }
  int c;
  while ((c = Serial1.read()) >= 0) {
    if (link_rx_byte(&uartRx, (uint8_t)c)) {
      cydSeen = true;
      queueSend(uartRx.type, uartRx.dev, uartRx.payload, uartRx.n);
    }
  }
}

void setup() {
  hostQueues();
  hostReady = true;
  Serial1.setFIFOSize(4096);
  Serial1.begin(LINK_BAUD);
  hub.begin(queueSend, pump);
}

void loop() {
  static bool box;
  hub.poll();
  const char* text = stickText();
  if (text) {
    if (!cydSeen) {
      midimap::loadText(text);
      midimap::push();
    }
    stickDone();
  }
  if (!cydSeen) {
    if (hub.box != box) {
      box = hub.box;
      hub.routeAll(true);
    }
    if (hub.changed) {  // the rules name devices: the box gets them again for what is there now
      hub.changed = false;
      midimap::push();
    }
  }
  delay(1);
}

void setup1() {
  while (!hostReady) {
  }
  hostSetup();
}

void loop1() {
  hostLoop();
}
