// Bluetooth MIDI (BLE MIDI 1.0): the CYD's own devices on the box (fm1link.h, LINK_USB and up), routed and mapped
// like the ones on its hub. Two ways:
// - a phone, tablet or computer connects to the CYD ("fm1buddy"): it's the device "bluetooth host"
// - the CYD connects to Bluetooth MIDI keyboards and synths: tap one on the BLUETOOTH page, and it's remembered,
//   and connected again whenever it's around
#pragma once

#include <Arduino.h>

#define BLE_NAME "fm1buddy"
#define BLE_HOST_NAME "bluetooth host"
#define BLE_PEERS 5  // found or remembered
#define BLE_SAVED 3  // remembered (the ESP32 has 3 connections)

struct BlePeer {
  char name[24];
  uint8_t addr[6], type;
  int8_t rssi;
  bool saved;
  uint32_t seenAt;  // found by the last scans (0: not since it started)
};

enum BleState { BLE_OFF,
                BLE_CONNECTING,
                BLE_ON };

namespace ble {
extern BlePeer peers[BLE_PEERS];
extern int count;

void begin(bool on);
void setOn(bool on);
bool isOn();
void pause(bool on);                                 // stop the stack while Wi-Fi is on (it needs the memory), and start it again
void pump();                                         // often (in hub.poll): what came in goes to the box, what the box sent goes out
void loop();                                         // in the main loop: connecting, scanning
void send(uint8_t d, const uint8_t* p, uint32_t n);  // MIDI from the box for device d (hub.own)
void search(bool on);                                // look for devices all the time (the BLUETOOTH page is open)
bool host();                                         // a phone or computer is connected to us
BleState state(int i);                               // peer i
bool shown(int i);                                   // remembered, connected, or around
void toggle(int i);                                  // remember it and connect, or forget it and disconnect
bool changed();                                      // the page should be drawn again (once)
void status(Print& p);                               // for the console
}  // namespace ble
