// The touch panel. The CYD's XPT2046, bit-banged: the display and the SD card have the two SPI hosts (after
// pntr_app_esp32.h). On the CYD-S3 (TOUCH_SDA): its FT6336, on I2C.
#pragma once

#include <Arduino.h>

#ifndef TOUCH_SDA
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_SCLK 25
#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3700
#define TOUCH_MIN_Y 240
#define TOUCH_MAX_Y 3800
#endif

void touchBegin();
// true while touched; x, y in screen pixels (320x240, landscape)
bool touchRead(int& x, int& y);
// true once per touch, when it starts; with repeat, also again and again while it is held (*repeat is
// then true)
bool touchTap(int& x, int& y, bool* repeat = 0);
