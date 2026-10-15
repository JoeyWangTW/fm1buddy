// The link to the USB host box (common/fm1link.h): the devices on its hub, the SysEx they send, sending to them
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

class Link {
 public:
  LinkDev dev[LINK_DEVS];
  bool box;  // the box answered
  uint32_t mounts;

  void begin();
  void poll();  // call often: reads the UART
  void send(uint8_t d, const uint8_t* p, uint32_t n);
  void route(const uint8_t r[LINK_DEVS]);
  void routeAll(bool on);
  // the next complete SysEx (F0..F7) from device d, or 0
  uint32_t sysex(uint8_t d, uint8_t* out);
  void drain(uint8_t d);

 private:
  link_rx_t rx;
  uint32_t helloAt;
  uint8_t sx[LINK_DEVS][SYSEX_MAX];
  uint32_t sxn[LINK_DEVS];
  bool sxOn[LINK_DEVS];
  uint8_t q[LINK_DEVS][SYSEX_Q][SYSEX_MAX];
  uint8_t qn[LINK_DEVS][SYSEX_Q];
  uint32_t qw[LINK_DEVS], qr[LINK_DEVS];
  void frame(uint8_t type, uint8_t d, const uint8_t* p, uint32_t n);
  void midi(uint8_t d, uint8_t b);
  void out(uint8_t type, uint8_t d, const uint8_t* p, uint32_t n);
};

extern Link hub;
