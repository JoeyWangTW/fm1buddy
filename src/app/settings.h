// The settings, kept across restarts
#pragma once

#include <stdint.h>

struct Settings {
  uint32_t version;
  char ssid[33];
  char pass[65];
  uint8_t theme;
  uint8_t brightness;  // percent
  bool hubAuto;        // update the Hub list the first time it's opened after power-on
};

extern Settings settings;

void settingsLoad();
void settingsSave();
