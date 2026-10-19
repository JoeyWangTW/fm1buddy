// The FM-1 Firmware Hub's catalogue (thefirmwarehub.com/catalog.json), over Wi-Fi: the packages that fit the FM-1,
// and downloading one to the SD card. From each entry of "firmwares" it uses name, author, version, pkg, product,
// size, sha256 and beta, and from "stock" (M-VAVE's own) pkg, product, size and sha256.
#pragma once

#include <Arduino.h>
#include <FS.h>

#include "fm1ota.h"

#define HUB_SITE "https://thefirmwarehub.com/"
#define HUB_MAX 96

struct HubPkg {
  char name[24];
  char author[40];
  char version[24];
  char pkg[64];      // its path on the site: "fw/zp12-2.4.4.fwsc"
  char product[24];  // "FM-1_974"
  char sha256[65];
  uint32_t size;
  bool beta;
  const char* file() const;  // its file name: "zp12-2.4.4.fwsc"
};

namespace fwhub {
extern HubPkg list[HUB_MAX];
extern int count;

// join a Wi-Fi network (idle() is called while it waits): error, or 0
const char* join(const char* ssid, const char* pass, void (*idle)());
// the catalogue's packages for this device (0: for any FM-1), M-VAVE's own first: error, or 0
const char* fetch(const Fm1Id* dev);
// the package to path, checked against its size and sha256 before it gets that name: error, or 0
const char* download(const HubPkg& p, fs::FS& fs, const char* path, void (*progress)(uint32_t done, uint32_t size));
}  // namespace fwhub
