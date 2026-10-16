// fm1buddy on the Adafruit Feather RP2040 with USB Type A Host: the USB MIDI host box (host.cpp, on core 1), and
// on core 0:
// - the USB drive: the Feather's USB-C shows its flash to a computer, for firmwares (fm1/) and midimap.txt
// - the link to a CYD on TX/RX (common/fm1link.h), passed through to the box
// - the MIDI mapping rules in midimap.txt on the drive (edited there, or on the screen)
// - with a 128x64 OLED and buttons (ui.cpp): installing firmware, the devices, MIDI thru and mapping
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <EEPROM.h>
#include <FatFS.h>
#include <SDFS.h>

#include "fm1link.h"
#include "host.h"
#include "link.h"
#include "midimap.h"
#include "ui.h"

bool sdOk, thru = true;
static Adafruit_USBD_MSC msc;
static volatile uint32_t mscWriteAt;  // the computer wrote to the drive: our view of it is old
static volatile bool hostReady;
static link_rx_t uartRx;
static bool ui;       // a screen is attached
static bool cydSeen;  // a CYD talks to the box: it sets the routing and mapping (from its own SD card)

static bool local() {  // this board sets the routing and the mapping
  return ui || !cydSeen;
}

// --------------------------------------------------------------------------------------- the USB drive ---
static uint16_t sectorSize = 512;

static int32_t mscRead(uint32_t lba, void* buf, uint32_t n) {
  return fatfs::disk_read(0, (fatfs::BYTE*)buf, lba, n / sectorSize) ? -1 : (int32_t)n;
}

static int32_t mscWrite(uint32_t lba, uint8_t* buf, uint32_t n) {
  mscWriteAt = millis() | 1;
  return fatfs::disk_write(0, buf, lba, n / sectorSize) ? -1 : (int32_t)n;
}

static void mscFlush() {
  fatfs::disk_ioctl(0, CTRL_SYNC, nullptr);
}

static void driveBegin() {
  FatFS.begin();  // formats it the first time
  if (!FatFS.exists("/fm1")) {
    FatFS.mkdir("/fm1");
  }
  if (!FatFS.exists(MAP_FILE)) {
    midimap::save(FatFS);  // no rules yet: just how to write them
  }
  FatFS.setConfig(FatFSConfig(false));  // never format it again by surprise (a remount while the computer writes)
  fatfs::LBA_t sectors;
  fatfs::WORD ss;
  fatfs::disk_ioctl(0, GET_SECTOR_COUNT, &sectors);
  fatfs::disk_ioctl(0, GET_SECTOR_SIZE, &ss);
  sectorSize = ss;
  msc.setID("fm1buddy", "Firmwares", "1.0");
  msc.setReadWriteCallback(mscRead, mscWrite, mscFlush);
  msc.setCapacity(sectors, sectorSize);
  msc.setUnitReady(true);
  msc.begin();
  if (TinyUSBDevice.mounted()) {  // already enumerated without the drive: again, with it
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
}

// a second after the computer last wrote: look at the drive again (new firmwares, an edited midimap.txt)
static void driveFresh() {
  if (mscWriteAt && millis() - mscWriteAt > 1000) {
    mscWriteAt = 0;
    FatFS.end();
    FatFS.begin();
    if (local()) {
      midimap::load(FatFS);
      midimap::push();
    }
    if (ui) {
      uiDriveChanged();
    }
  }
}

// ---------------------------------------------------------------------------------------------- links ---
// the screen's link (hub) goes to core 1 through the queue
static void queueSend(uint8_t type, uint8_t d, const uint8_t* p, uint32_t n) {
  static HostFrame f;
  f.type = type;
  f.dev = d;
  f.n = (uint16_t)(n > LINK_PAYLOAD ? LINK_PAYLOAD : n);
  memcpy(f.p, p, f.n);
  queue_add_blocking(&toHost, &f);
}

// what the box says goes to the screen and to a CYD; what a CYD says goes to the box
static void pump() {
  static HostFrame f;
  uint8_t wire[LINK_WIRE];
  while (queue_try_remove(&fromHost, &f)) {
    hub.frame(f.type, f.dev, f.p, f.n);
    Serial1.write(wire, link_encode(f.type, f.dev, f.p, f.n, wire));
  }
  int c;
  while ((c = Serial1.read()) >= 0) {
    if (link_rx_byte(&uartRx, (uint8_t)c)) {
      cydSeen = true;
      queueSend(uartRx.type, uartRx.dev, uartRx.payload, uartRx.n);
    }
  }
}

void saveThru() {
  EEPROM.write(0, thru ? 1 : 2);
  EEPROM.commit();
}

// ------------------------------------------------------------------------------------------------ run ---
void setup() {
  hostQueues();
  hostReady = true;
  driveBegin();
  Serial.begin(115200);
  Serial1.setFIFOSize(4096);
  Serial1.begin(LINK_BAUD);
  EEPROM.begin(256);
  thru = EEPROM.read(0) != 2;
  SDFS.setConfig(SDFSConfig(SD_CS, SPI_HALF_SPEED, SPI));
  sdOk = SDFS.begin();
  hub.begin(queueSend, pump);
  midimap::load(FatFS);
  ui = uiBegin();
}

void loop() {
  static bool box;
  hub.poll();
  if (local()) {
    if (hub.box != box) {
      box = hub.box;
      hub.routeAll(thru);
    }
    if (hub.changed) {  // the rules name devices: the box gets them again for what is there now
      hub.changed = false;
      midimap::push();
    }
  }
  if (ui) {
    uiLoop();
  }
  driveFresh();
  delay(1);
}

void setup1() {
  while (!hostReady) {
  }
  hostSetup();
}

void loop1() {
  hostLoop();
}
