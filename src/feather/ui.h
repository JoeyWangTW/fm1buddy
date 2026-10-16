// The Feather's own screen: a 128x64 I2C OLED and up / OK / down (/ back) buttons
#pragma once

// false: no screen (the box works on its own)
bool uiBegin();
void uiLoop();
// the computer changed the USB drive: list the firmwares again
void uiDriveChanged();
