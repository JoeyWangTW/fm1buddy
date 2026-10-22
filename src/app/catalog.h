// The firmwares: the FM-1 Firmware Hub's list (thefirmwarehub.com/catalog.json, kept on the SD card) and the
// .fwsc files in the SD card's /fm1 folder
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#define HUB_SITE "https://thefirmwarehub.com/"
#define FW_DIR "/fm1"
#define DATA_DIR "/fm1buddy"
#define HUB_CACHE DATA_DIR "/hub.json"
#define HUB_ETAG DATA_DIR "/hub.etag"

struct Firmware {
  std::string name, author, version, date, what;
  std::string pkg;      // on the Hub: "fw/zp12-2.4.4.fwsc"
  std::string file;     // its file name: "zp12-2.4.4.fwsc"
  std::string product;  // the device it's for: "FM-1_974"
  std::string sha256;
  std::vector<std::string> kinds;  // "synth", "groove", "drums", ...
  uint32_t size = 0, stars = 0, installs = 0;
  bool beta = false, stock = false, keyboard = false, mouse = false;
  bool onSd = false;  // a Hub firmware that is in /fm1 too
  bool hub = false;   // on the SD card: its details came from the Hub's list
};

namespace catalog {
extern std::vector<Firmware> hub, sd;
extern std::string hubUpdated;  // when the Hub's list was made: "2026-10-22 05:01 UTC"

// a path on the SD card ("/fm1/x.fwsc" -> "/sdcard/fm1/x.fwsc"), "" without a card
std::string path(const std::string& p);
// read the Hub's list from the SD card into out: false (err says why) if it isn't there or doesn't read
bool parseHub(std::vector<Firmware>& out, std::string& updated, std::string& err);
// the files in /fm1, with what the Hub's list knows about them; marks the Hub's that are there
void scanSd();
// the device a package is for, from its own header ("FM-1_974"), "" if it isn't a package
std::string product(const char* path);
}  // namespace catalog
