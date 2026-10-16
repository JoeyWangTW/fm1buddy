// The link to the USB host box (fm1link.h): the devices on its hub, the SysEx they send, sending to them. The
// transport is the board's: the CYD's UART to the box, or the Feather's queue between its two cores
#pragma once

#include <Arduino.h>

#include "fm1link.h"

#define SYSEX_MAX 96  // the longest SysEx kept (the FM-1's are 19 and 41 bytes on the wire)
#define SYSEX_Q 8

struct LinkDev {
  bool on;
  uint16_t vid, pid;
  char name[32];
  uint32_t serial;  // changes on every mount, so a new device in the same slot is noticed
};

// sends one frame to the box
typedef void (*LinkSend)(uint8_t type, uint8_t d, const uint8_t* p, uint32_t n);
// hands what came from the box to hub.frame()
typedef void (*LinkPump)();

class Link {
 public:
  LinkDev dev[LINK_DEVS];
  bool box;  // the box answered
  uint32_t mounts;
  bool changed;  // a device came or went (or the box started): set here, cleared by whoever cares
  // the newest channel message from any device (for learning a mapping): seq goes up with each
  uint32_t seq;
  uint8_t lastDev, last[3];

  void begin(LinkSend send, LinkPump pump);
  void poll();  // call often: reads the UART
  void send(uint8_t d, const uint8_t* p, uint32_t n);
  void route(const uint8_t r[LINK_DEVS]);
  void routeAll(bool on);
  void map(const uint8_t* rules, uint32_t n);
  // a frame as if from the box (also the console's way of faking one)
  void frame(uint8_t type, uint8_t d, const uint8_t* p, uint32_t n);
  // the next complete SysEx (F0..F7) from device d, or 0
  uint32_t sysex(uint8_t d, uint8_t* out);
  void drain(uint8_t d);

 private:
  LinkSend sendFn;
  LinkPump pumpFn;
  uint32_t helloAt;
  uint8_t sx[LINK_DEVS][SYSEX_MAX];
  uint32_t sxn[LINK_DEVS];
  bool sxOn[LINK_DEVS];
  uint8_t q[LINK_DEVS][SYSEX_Q][SYSEX_MAX];
  uint8_t qn[LINK_DEVS][SYSEX_Q];
  uint32_t qw[LINK_DEVS], qr[LINK_DEVS];
  uint8_t st[LINK_DEVS], data[LINK_DEVS][2], nd[LINK_DEVS];  // channel messages, as they come in
  void midi(uint8_t d, uint8_t b);
  void out(uint8_t type, uint8_t d, const uint8_t* p, uint32_t n);
};

extern Link hub;
