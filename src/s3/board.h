// The CYD-S3's own parts that the app's hal.h doesn't cover
#pragma once

#include <stdint.h>

namespace board {
// the SD card, the LED off, and the USB-C port: a serial port if a computer is on it, else the USB host
void begin();
void usbBegin();      // the USB host, unless a computer is on the port (after the screen is up)
void usbHostBegin();  // usb_s3.cpp
void diskBegin();     // disk_s3.cpp: the USB port as a USB drive (the SD card), in USB disk mode
void loop();
// testing over the serial console: "host" starts as the USB host once (for 40 s), "log" prints the SD card's log.
// script: what to do meanwhile, "tap X Y", "wait MS" and "usbrestart" (hal::usbRestart) between ';' (the console
// can't: the port is the host then)
void hostTestStart(const char* script);
const char* hostTestScript();
void printLog();
// USB MIDI for a computer (computer_s3.cpp): the port as a USB MIDI device, it's on, MIDI to it (device 0)
void computerBegin();
bool computerOn();
uint32_t computerOut(const uint8_t* packets, uint32_t n);
// Bluetooth MIDI (ble_s3.cpp): its loop (the main loop), stopping it while Wi-Fi is on, and MIDI to devices 4-7
void bleLoop();
void blePause(bool on);
uint32_t bleOut(int d, const uint8_t* packets, uint32_t n);
}  // namespace board
