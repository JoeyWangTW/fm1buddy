#include "settings.h"

#include <string.h>

#include "hal.h"
#include "themes.h"

#define VERSION 1

Settings settings;

void settingsLoad() {
  if (hal::load("settings", &settings, sizeof settings) && settings.version == VERSION) {
    if (settings.theme >= THEME_COUNT) {
      settings.theme = 0;
    }
    return;
  }
  memset(&settings, 0, sizeof settings);
  settings.version = VERSION;
  settings.theme = 2;  // BLUE
  settings.brightness = 80;
  settings.hubAuto = true;
}

void settingsSave() {
  hal::save("settings", &settings, sizeof settings);
}
