// The CYD-S3's own parts that the app's hal.h doesn't cover
#pragma once

#include <stdint.h>

namespace board {
// the SD card, the LED off, and the USB-C port: a serial port if a computer is on it, else the USB host
void begin();
void usbBegin();      // the USB host, unless a computer is on the port (after the screen is up)
void usbHostBegin();  // usb_s3.cpp
void loop();
// testing over the serial console: "host" starts as the USB host once (for 20 s), "log" prints the SD card's log
void hostTestStart();
void printLog();
// Bluetooth MIDI (ble_s3.cpp): its loop (the main loop), stopping it while Wi-Fi is on, and MIDI to devices 4-7
void bleLoop();
void blePause(bool on);
uint32_t bleOut(int d, const uint8_t* packets, uint32_t n);
}  // namespace board
