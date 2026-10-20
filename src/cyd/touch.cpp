#include "touch.h"

#ifdef TOUCH_SDA
#include <Wire.h>

#define FT6336 0x38

void touchBegin() {
  pinMode(TOUCH_INT, INPUT);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, 0);
  delay(10);
  digitalWrite(TOUCH_RST, 1);
  delay(300);  // it starts
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
}

bool touchRead(int& x, int& y) {
  uint8_t r[5];  // TD_STATUS, then the first point: XH XL YH YL
  Wire.beginTransmission(FT6336);
  Wire.write(0x02);
  if (Wire.endTransmission(false) || Wire.requestFrom(FT6336, 5) != 5) {
    return false;
  }
  for (uint8_t& b : r) {
    b = Wire.read();
  }
  if ((r[0] & 0x0F) == 0 || (r[0] & 0x0F) > 2) {
    return false;
  }
  int px = (r[1] & 0x0F) << 8 | r[2], py = (r[3] & 0x0F) << 8 | r[4];
  // the panel is portrait (240x320): turned as the screen is (rotation 1)
  x = py;
  y = 239 - px;
  return x >= 0 && x < 320 && y >= 0 && y < 240;
}
#else
static int clock1() {
  digitalWrite(TOUCH_SCLK, 1);
  delayMicroseconds(1);
  int bit = digitalRead(TOUCH_MISO);
  digitalWrite(TOUCH_SCLK, 0);
  delayMicroseconds(1);
  return bit;
}

static int channel(uint8_t cmd) {
  digitalWrite(TOUCH_CS, 0);
  delayMicroseconds(1);
  for (int i = 7; i >= 0; i--) {
    digitalWrite(TOUCH_MOSI, (cmd >> i) & 1);
    delayMicroseconds(1);
    clock1();
  }
  int v = 0;
  for (int i = 0; i < 16; i++) {
    v = v << 1 | clock1();
  }
  digitalWrite(TOUCH_CS, 1);
  return (v >> 3) & 0x0FFF;
}

void touchBegin() {
  pinMode(TOUCH_MOSI, OUTPUT);
  pinMode(TOUCH_SCLK, OUTPUT);
  pinMode(TOUCH_CS, OUTPUT);
  pinMode(TOUCH_MISO, INPUT);
  pinMode(TOUCH_IRQ, INPUT);
  digitalWrite(TOUCH_CS, 1);
  digitalWrite(TOUCH_SCLK, 0);
}

bool touchRead(int& x, int& y) {
  if (digitalRead(TOUCH_IRQ)) {
    return false;
  }
  channel(0xD0);  // the first sample of each axis settles late
  int rx = channel(0xD0);
  channel(0x90);
  int ry = channel(0x90);
  if (rx <= 0 || ry <= 0) {
    return false;
  }
  // the panel's axes are swapped against the landscape screen
  x = (ry - TOUCH_MIN_Y) * 320 / (TOUCH_MAX_Y - TOUCH_MIN_Y);
  y = (rx - TOUCH_MIN_X) * 240 / (TOUCH_MAX_X - TOUCH_MIN_X);
  return x >= 0 && x < 320 && y >= 0 && y < 240;
}
#endif

bool touchTap(int& x, int& y, bool* repeat) {
  static bool down;
  static uint32_t upAt, downAt, repAt;
  int tx, ty;
  uint32_t now = millis();
  if (!touchRead(tx, ty)) {
    if (down && now - upAt > 60) {
      down = false;
    }
    return false;
  }
  upAt = now;
  if (!down) {
    down = true;
    downAt = repAt = now;
    if (repeat) {
      *repeat = false;
    }
  } else if (repeat && now - downAt > 400 && now - repAt > 70) {
    repAt = now;
    *repeat = true;
  } else {
    return false;
  }
  x = tx;
  y = ty;
  return true;
}
