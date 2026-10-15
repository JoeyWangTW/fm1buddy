// The CYD's XPT2046 touch panel, bit-banged: the display and the SD card have the two SPI hosts
// (after pntr_app_esp32.h)
#pragma once

#include <Arduino.h>

#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_SCLK 25
#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3700
#define TOUCH_MIN_Y 240
#define TOUCH_MAX_Y 3800

void touchBegin();
// true while touched; x, y in screen pixels (320x240, landscape)
bool touchRead(int& x, int& y);
// true once per touch, when it starts
bool touchTap(int& x, int& y);
