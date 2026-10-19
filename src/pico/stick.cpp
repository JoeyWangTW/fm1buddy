// The USB stick (stick.h): SdFat, on a block device that reads through TinyUSB's mass storage host
#include "stick.h"

#include <Adafruit_TinyUSB.h>
#include <SdFat.h>

static char text[STICK_TEXT + 1];
static volatile bool ready;    // text holds a midimap.txt that core 0 hasn't read
static volatile uint8_t want;  // a stick's address, mounted and not read yet

class StickBlocks : public FsBlockDeviceInterface {
 public:
  uint8_t daddr;
  bool isBusy() override { return busy; }
  bool readSector(Sector_t s, uint8_t* dst) override { return readSectors(s, dst, 1); }
  bool readSectors(Sector_t s, uint8_t* dst, size_t ns) override {
    busy = true;
    ok = false;
    if (!tuh_msc_read10(daddr, 0, dst, s, (uint16_t)ns, done, (uintptr_t)this)) {
      busy = false;
      return false;
    }
    while (busy && tuh_msc_mounted(daddr)) {
      tuh_task();
    }
    return !busy && ok;
  }
  Sector_t sectorCount() override { return tuh_msc_get_block_count(daddr, 0); }
  bool syncDevice() override { return true; }
  bool writeSector(Sector_t, const uint8_t*) override { return false; }  // only ever read
  bool writeSectors(Sector_t, const uint8_t*, size_t) override { return false; }

 private:
  volatile bool busy, ok;
  static bool done(uint8_t, const tuh_msc_complete_data_t* d) {
    StickBlocks* b = (StickBlocks*)d->user_arg;
    b->ok = d->csw->status == 0;
    b->busy = false;
    return true;
  }
};

static StickBlocks blocks;
static FsVolume vol;

void tuh_msc_mount_cb(uint8_t daddr) {
  want = daddr;  // read in stickTask(), not in here
}

void tuh_msc_umount_cb(uint8_t daddr) {
  if (want == daddr) {
    want = 0;
  }
}

void stickTask() {
  if (!want || ready || tuh_msc_get_block_size(want, 0) != 512) {
    return;
  }
  blocks.daddr = want;
  want = 0;
  if (!vol.begin(&blocks)) {
    return;
  }
  FsFile f = vol.open("/midimap.txt", O_RDONLY);
  if (!f) {
    return;
  }
  int n = f.read(text, STICK_TEXT);
  f.close();
  text[n > 0 ? n : 0] = 0;
  __dmb();
  ready = true;
}

const char* stickText() {
  if (!ready) {
    return 0;
  }
  __dmb();
  return text;
}

void stickDone() {
  __dmb();
  ready = false;
}
