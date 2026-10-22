// fm1buddy: pick a firmware from SD:/fm1 (or the FM-1 Firmware Hub, over Wi-Fi) and install it on the FM-1,
// through the USB MIDI host box. Bluetooth MIDI devices (ble.h) join the box's devices. On the CYD, the box is
// another board, on CN1's UART; on the CYD-S3 (CYD_S3) it is this one (s3box.h), its USB-C port the host.
#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#ifdef CYD_S3
#include <SD_MMC.h>
#define SD SD_MMC
#include "s3box.h"
#define NO_BOX "USB host didn't start"
#define HEAD_NO_BOX "no USB host"
#define ON_HUB "Plug the FM-1 into the hub."
#define EMPTY_HUB "Nothing on the hub"
#else
#include <SD.h>
#define NO_BOX "Host box not connected (CN1)"
#define HEAD_NO_BOX "no host box"
#define ON_HUB "Plug the FM-1 into the host box's hub."
#define EMPTY_HUB "Nothing plugged into the host box"
#endif

#include "ble.h"
#include "fm1ota.h"
#include "fwhub.h"
#include "link.h"
#include "midimap.h"
#include "touch.h"

#define DIR "/fm1"
#define WIFI_FILE "/wifi.txt"
#define MAX_FILES 64
#define W 320
#define H 240
#define HEAD 24
#define FOOT 34
#define ROW 30
#define ROWS ((H - HEAD - FOOT) / ROW)

#define C_BG TFT_BLACK
#define C_FG TFT_WHITE
#define C_DIM 0x7BEF
#define C_BAR 0x18E3
#define C_ACCENT 0xFD20
#define C_OK 0x07E0
#define C_BAD 0xF800

static TFT_eSPI tft;
#ifndef CYD_S3
static SPIClass sdSpi(VSPI);
#endif
static bool sdOk;

static struct {
  char name[48];
  char product[24];  // empty: not an FM-1 package
} files[MAX_FILES];
static int nFiles, top, sel;

enum Page { FILES,
            CONFIRM,
            RUN,
            DEVICES,
            MAP,
            RULE,
            CATALOG,  // the Firmware Hub's packages
            ITEM,     // one of them
            GET,      // downloading it
            WIFI,     // the networks
            KEYS,     // typing a password
            BT,       // Bluetooth MIDI
            MSG };
static Page page;
static bool thru = true;  // MIDI between the devices on the hub (saved)
static Preferences prefs;

// ------------------------------------------------------------------------------------------ drawing ---
static void text(const char* s, int x, int y, int font, uint16_t color, uint8_t datum = ML_DATUM, int maxW = W) {
  char buf[64];
  strlcpy(buf, s, sizeof buf);
  tft.setTextFont(font);
  for (size_t n = strlen(buf); n > 1 && tft.textWidth(buf) > maxW; n--) {
    buf[n - 2] = '~';
    buf[n - 1] = 0;
  }
  tft.setTextColor(color, C_BG);
  tft.setTextDatum(datum);
  tft.drawString(buf, x, y);
}

// a message over as many centered lines as it needs, broken at spaces
static void wrap(const char* s, int y) {
  char line[64];
  tft.setTextFont(2);
  while (*s) {
    int n = 0, cut = 0;
    while (s[n] && n < (int)sizeof line - 1) {
      line[n] = s[n];
      line[n + 1] = 0;
      if (tft.textWidth(line) > W - 20) {
        break;
      }
      if (s[n] == ' ') {
        cut = n;
      }
      n++;
    }
    if (s[n] && cut) {
      n = cut;
    }
    line[n] = 0;
    text(line, W / 2, y, 2, C_FG, MC_DATUM);
    y += 18;
    s += n;
    while (*s == ' ') {
      s++;
    }
  }
}

static void button(int x, int w, const char* label, uint16_t color) {
  tft.fillRoundRect(x + 2, H - FOOT + 3, w - 4, FOOT - 6, 6, color);
  tft.setTextFont(2);
  tft.setTextColor(C_BG, color);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, x + w / 2, H - FOOT / 2);
}

static bool fits(int d) {  // the selected package is for device d's model
  Pkg pkg;
  strlcpy(pkg.product, files[sel].product, sizeof pkg.product);
  return pkg.fits(*fm1::known(d));
}

static char shownStatus[48];

static void header(bool force = false) {
  char s[48];
  int d = fm1::current();
  if (!hub.box) {
    strlcpy(s, HEAD_NO_BOX, sizeof s);
  } else if (d < 0) {
    strlcpy(s, "no FM-1", sizeof s);
  } else {
    strlcpy(s, fm1::known(d)->text, sizeof s);
  }
  if (!force && !strcmp(s, shownStatus)) {
    return;
  }
  strlcpy(shownStatus, s, sizeof shownStatus);
  tft.fillRect(0, 0, W, HEAD, C_BAR);
  tft.setTextFont(2);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_ACCENT, C_BAR);
  tft.drawString("fm1buddy", 6, HEAD / 2);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(!hub.box ? C_BAD : d < 0 ? C_DIM
                                            : C_OK,
                   C_BAR);
  tft.drawString(s, W - 6, HEAD / 2);
}

static void body() {
  tft.fillRect(0, HEAD, W, H - HEAD, C_BG);
}

static void drawFiles() {
  body();
  if (!sdOk) {
    text("No SD card", W / 2, 100, 4, C_BAD, MC_DATUM);
  } else if (!nFiles) {
    text("No files in " DIR, W / 2, 100, 4, C_DIM, MC_DATUM);
  }
  for (int r = 0; r < ROWS && top + r < nFiles; r++) {
    int i = top + r, y = HEAD + r * ROW;
    text(files[i].name, 8, y + ROW / 2, 2, C_FG, ML_DATUM, 200);
    text(files[i].product[0] ? files[i].product : "not FM-1", W - 8, y + ROW / 2, 2,
         files[i].product[0] ? C_ACCENT : C_DIM, MR_DATUM);
    tft.drawFastHLine(0, y + ROW - 1, W, C_BAR);
  }
  button(0, 64, "UP", C_DIM);
  button(64, 64, "DOWN", C_DIM);
  button(128, 96, "ONLINE", C_DIM);
  button(224, 96, "DEVICES", C_ACCENT);
}

static void drawConfirm() {
  body();
  int d = fm1::current();
  text(files[sel].name, 8, HEAD + 20, 4, C_FG, ML_DATUM, W - 16);
  text("package", 8, HEAD + 54, 2, C_DIM);
  text(files[sel].product, 100, HEAD + 54, 2, C_ACCENT);
  text("device", 8, HEAD + 78, 2, C_DIM);
  text(d < 0 ? "none" : fm1::known(d)->text, 100, HEAD + 78, 2, d < 0 ? C_BAD : C_OK);
  const char* label = "INSTALL";
  bool can = d >= 0;
  if (d < 0) {
    text(ON_HUB, 8, HEAD + 120, 2, C_DIM);
  } else if (fm1::known(d)->ota()) {
    label = "FINISH";
    text("The FM-1 is in update mode: FINISH", 8, HEAD + 120, 2, C_FG);
    text("writes this package.", 8, HEAD + 140, 2, C_FG);
  } else {
    if (!fits(d)) {
      can = false;
      text("This package is for another model.", 8, HEAD + 120, 2, C_BAD);
    } else {
      text("Keep the FM-1 plugged in until it is done.", 8, HEAD + 120, 2, C_DIM);
    }
  }
  button(0, 160, "BACK", C_DIM);
  button(160, 160, label, can ? C_OK : C_BAR);
}

#define DEV_ROW 22  // all LINK_DEVS of them fit

static void drawDevices() {
  body();
  int y = HEAD + 2;
  for (int d = 0; d < LINK_DEVS; d++) {
    if (!hub.dev[d].on) {
      continue;
    }
    char s[24];
    const char* what = s;
    uint16_t c = C_DIM;
    snprintf(s, sizeof s, "%04X:%04X", hub.dev[d].vid, hub.dev[d].pid);
    if (fm1::known(d)) {
      what = fm1::known(d)->text;
      c = C_OK;
    } else if (d >= LINK_USB) {
      what = "Bluetooth";
      c = C_ACCENT;
    }
    text(hub.dev[d].name[0] ? hub.dev[d].name : "(no name)", 8, y + DEV_ROW / 2, 2, C_FG, ML_DATUM, 180);
    text(what, W - 8, y + DEV_ROW / 2, 2, c, MR_DATUM, 124);
    y += DEV_ROW;
  }
  if (!hub.box) {
    text(NO_BOX, W / 2, 100, 2, C_BAD, MC_DATUM);
  } else if (y == HEAD + 2) {
    text(EMPTY_HUB, W / 2, 100, 2, C_DIM, MC_DATUM);
  }
  button(0, 88, thru ? "THRU: ON" : "THRU: OFF", thru ? C_OK : C_DIM);
  button(88, 88, "BLUETOOTH", ble::isOn() ? C_ACCENT : C_DIM);
  button(176, 72, "MAP", C_DIM);
  button(248, 72, "FILES", C_ACCENT);
}

// ------------------------------------------------------------------------------------------ Bluetooth ---
// the first row is a phone or computer connecting to us; then the keyboards and synths (shown[] maps rows)
static int shown[BLE_PEERS], nShown;

static void drawBt() {
  body();
  int y = HEAD;
  const char* host = !ble::isOn() ? "off" : ble::host() ? "connected"
                                                        : "visible as " BLE_NAME;
  text("Phone or computer", 8, y + ROW / 2, 2, C_FG);
  text(host, W - 8, y + ROW / 2, 2, !ble::isOn() ? C_DIM : ble::host() ? C_OK
                                                                       : C_ACCENT,
       MR_DATUM);
  tft.drawFastHLine(0, y + ROW - 1, W, C_DIM);
  nShown = 0;
  for (int i = 0; i < ble::count && nShown < ROWS - 1; i++) {
    if (!ble::shown(i)) {
      continue;
    }
    shown[nShown++] = i;
    y += ROW;
    const BlePeer& p = ble::peers[i];
    BleState st = ble::state(i);
    const char* s = st == BLE_ON ? "connected" : st == BLE_CONNECTING ? "connecting"
                                             : p.saved                ? "saved"
                                                                      : "tap to connect";
    text(p.name, 8, y + ROW / 2, 2, p.saved ? C_FG : C_DIM, ML_DATUM, 190);
    text(s, W - 8, y + ROW / 2, 2, st == BLE_ON ? C_OK : st == BLE_CONNECTING ? C_ACCENT
                                                                              : C_DIM,
         MR_DATUM);
    tft.drawFastHLine(0, y + ROW - 1, W, C_BAR);
  }
  if (hub.box && hub.version < 2) {
    wrap("Update the host box's firmware: this one doesn't know Bluetooth devices.", HEAD + ROW + 30);
  } else if (!ble::isOn()) {
    text("Bluetooth is off", W / 2, HEAD + ROW + 50, 2, C_DIM, MC_DATUM);
  } else if (!nShown) {
    text("Looking for Bluetooth MIDI devices...", W / 2, HEAD + ROW + 50, 2, C_DIM, MC_DATUM);
  }
  button(0, 160, ble::isOn() ? "BLUETOOTH: ON" : "BLUETOOTH: OFF", ble::isOn() ? C_OK : C_DIM);
  button(160, 160, "BACK", C_ACCENT);
}

// --------------------------------------------------------------------------------------------- mapping ---
#define CELL_H 44
static int mapTop, editIdx;  // editIdx -1: a new rule
static MapRule edit;
static bool learning;
static uint32_t learnSeq;

static void drawMap() {
  body();
  if (!midimap::count) {
    text("No mappings", W / 2, 90, 4, C_DIM, MC_DATUM);
    text("Everything passes as it is.", W / 2, 122, 2, C_DIM, MC_DATUM);
  }
  for (int r = 0; r < ROWS && mapTop + r < midimap::count; r++) {
    int i = mapTop + r, y = HEAD + r * ROW;
    char s[128], n[4];
    midimap::format(midimap::rules[i], s, sizeof s);
    snprintf(n, sizeof n, "%d", i + 1);
    text(n, 8, y + ROW / 2, 2, C_DIM);
    text(s, 30, y + ROW / 2, 2, C_FG, ML_DATUM, W - 38);
    tft.drawFastHLine(0, y + ROW - 1, W, C_BAR);
  }
  button(0, 64, "UP", C_DIM);
  button(64, 64, "DOWN", C_DIM);
  button(128, 96, "ADD", midimap::count < MAP_RULES ? C_OK : C_BAR);
  button(224, 96, "BACK", C_ACCENT);
}

// one field of the rule: its label, its value, and - + to change it
static void cell(int col, int row, const char* label, const char* value, bool on) {
  int x = col * 160, y = HEAD + row * CELL_H;
  tft.fillRect(x, y, 160, CELL_H, C_BG);
  text(label, x + 6, y + 7, 1, C_DIM);
  uint16_t c = on ? C_DIM : C_BAR;
  tft.fillRoundRect(x + 4, y + 14, 34, 28, 5, c);
  tft.fillRoundRect(x + 122, y + 14, 34, 28, 5, c);
  tft.setTextFont(4);
  tft.setTextColor(C_BG, c);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("-", x + 21, y + 28);
  tft.drawString("+", x + 139, y + 28);
  text(value, x + 80, y + 28, 2, on ? C_FG : C_DIM, MC_DATUM, 80);
}

static void drawRule() {
  body();
  char b[8];
  bool drop = edit.outType == MAP_DROP;
  for (int f = 0; f < 8; f++) {
    cell(f / 4, f % 4, midimap::label(edit, f), f == 4 && drop ? "drop it" : midimap::show(edit, f, b, sizeof b),
         midimap::active(edit, f));
  }
  tft.drawFastVLine(160, HEAD, H - HEAD - FOOT, C_BAR);
  button(0, 80, learning ? "LISTEN.." : "LEARN", learning ? C_ACCENT : C_DIM);
  button(80, 80, "DROP", drop ? C_BAD : C_DIM);
  button(160, 80, editIdx < 0 ? "CANCEL" : "DELETE", C_DIM);
  button(240, 80, "DONE", C_OK);
}

static void storeMap() {
  if (!midimap::save(SD)) {
    Serial.println("can't write " MAP_FILE);
  }
  midimap::push();
}

static void scan();
static void drawCatalog();
static void drawItem();
static void drawWifi();
static void drawKeys();

static void show(Page p) {
  if (p == FILES && page != FILES) {
    scan();  // something may have been downloaded
  }
  if (p == FILES || p == CONFIRM) {
    WiFi.mode(WIFI_OFF);
    ble::pause(false);
  }
  ble::search(p == BT);
  page = p;
  if (p == CATALOG) {
    drawCatalog();
  } else if (p == ITEM) {
    drawItem();
  } else if (p == WIFI) {
    drawWifi();
  } else if (p == KEYS) {
    drawKeys();
  } else if (p == FILES) {
    drawFiles();
  } else if (p == CONFIRM) {
    drawConfirm();
  } else if (p == DEVICES) {
    drawDevices();
  } else if (p == MAP) {
    drawMap();
  } else if (p == RULE) {
    drawRule();
  } else if (p == BT) {
    drawBt();
  }
}

// ------------------------------------------------------------------------------------------ install ---
static Pkg* running;
static uint32_t lastDraw;

static void progress(Fm1Step step, uint32_t served, uint32_t addr) {
  static const char* NAMES[] = {"Starting", "Checking", "Starting the loader", "Writing", "Restarting", "Done"};
  if (step == STEP_VERIFY || step == STEP_WRITE) {
    if (millis() - lastDraw < 100) {
      return;
    }
  } else {
    tft.fillRect(0, HEAD + 40, W, 90, C_BG);
  }
  lastDraw = millis();
  text(NAMES[step], W / 2, HEAD + 56, 4, C_FG, MC_DATUM);
  if (step == STEP_WRITE) {
    uint32_t w = (uint64_t)(addr + 1) * (W - 40) / running->size;
    tft.drawRect(20, HEAD + 84, W - 40, 20, C_DIM);
    tft.fillRect(21, HEAD + 85, w > W - 42 ? W - 42 : w, 18, C_ACCENT);
  }
  char s[32];
  snprintf(s, sizeof s, "   %u   ", (unsigned)served);
  if (served) {
    text(s, W / 2, HEAD + 120, 2, C_DIM, MC_DATUM);
  }
}

static void runInstall(bool resume) {
  page = RUN;
  body();
  header(true);
  text(files[sel].name, W / 2, HEAD + 20, 2, C_DIM, MC_DATUM);
  static Pkg pkg;
  char path[64];
  snprintf(path, sizeof path, DIR "/%s", files[sel].name);
  const char* err = pkg.open(SD, path) ? 0 : "Can't read the package.";
  if (!err) {
    running = &pkg;
    err = resume ? fm1::resume(pkg, progress) : fm1::install(pkg, progress);
    running = 0;
  }
  pkg.f.close();
  hub.routeAll(thru);
  tft.fillRect(0, HEAD + 40, W, H - HEAD - 40, C_BG);
  if (err) {
    text("Failed", W / 2, HEAD + 56, 4, C_BAD, MC_DATUM);
    wrap(err, HEAD + 92);
  } else {
    text("Installed", W / 2, HEAD + 56, 4, C_OK, MC_DATUM);
    text(files[sel].product, W / 2, HEAD + 92, 2, C_FG, MC_DATUM);
  }
  fm1::forget();  // the FM-1 restarted: ask it again
  button(0, W, "OK", C_ACCENT);
}

// --------------------------------------------------------------------------------- the Firmware Hub ---
static int hubTop, hubSel, netTop, nNets, keyMode, gotIdx;
static char ssid[33], pass[65], typed[65];
static struct {
  char ssid[33];
  bool open;
} nets[20];
static Page msgBack;

static void idle() {
  hub.poll();
}

static void busy(const char* s) {
  body();
  header();
  text(s, W / 2, 110, 2, C_FG, MC_DATUM);
}

// a message, and OK back to a page
static void message(const char* title, const char* why, Page back) {
  page = MSG;
  msgBack = back;
  body();
  text(title, W / 2, HEAD + 36, 4, C_BAD, MC_DATUM);
  wrap(why, HEAD + 76);
  button(0, W, "OK", C_ACCENT);
}

static void pkgPath(const HubPkg& p, char* path, size_t n) {
  snprintf(path, n, DIR "/%s", p.file());
}

static bool onSd(const HubPkg& p) {
  char path[96];
  pkgPath(p, path, sizeof path);
  if (!SD.exists(path)) {
    return false;
  }
  File f = SD.open(path);
  bool same = f && f.size() == p.size;
  f.close();
  return same;
}

static void drawCatalog() {
  body();
  for (int r = 0; r < ROWS && hubTop + r < fwhub::count; r++) {
    const HubPkg& p = fwhub::list[hubTop + r];
    int y = HEAD + r * ROW;
    char v[40];
    snprintf(v, sizeof v, "%s%s", p.version, p.beta ? " beta" : "");
    text(p.name, 8, y + ROW / 2, 2, C_FG, ML_DATUM, 170);
    text(v, W - 8, y + ROW / 2, 2, onSd(p) ? C_OK : p.beta ? C_DIM
                                                           : C_ACCENT,
         MR_DATUM, 130);
    tft.drawFastHLine(0, y + ROW - 1, W, C_BAR);
  }
  button(0, 64, "UP", C_DIM);
  button(64, 64, "DOWN", C_DIM);
  button(128, 96, "WI-FI", C_DIM);
  button(224, 96, "BACK", C_ACCENT);
}

static void drawItem() {
  body();
  const HubPkg& p = fwhub::list[hubSel];
  char s[48];
  text(p.name, 8, HEAD + 20, 4, C_FG, ML_DATUM, W - 16);
  const char* rows[][2] = {{"by", p.author}, {"version", p.version}, {"for", p.product}, {"size", s}};
  snprintf(s, sizeof s, "%u KB", (unsigned)(p.size + 1023) / 1024);
  for (int i = 0; i < 4; i++) {
    text(rows[i][0], 8, HEAD + 50 + i * 20, 2, C_DIM);
    text(rows[i][1], 90, HEAD + 50 + i * 20, 2, i == 2 ? C_ACCENT : C_FG, ML_DATUM, W - 98);
  }
  if (p.beta) {
    text("A beta: not finished yet.", 8, HEAD + 140, 2, C_DIM);
  }
  if (onSd(p)) {
    text("It's on the SD card already.", 8, HEAD + 160, 2, C_OK);
  }
  button(0, 160, "BACK", C_DIM);
  button(160, 160, "DOWNLOAD", C_OK);
}

static void getProgress(uint32_t done, uint32_t size) {
  if (millis() - lastDraw < 100 && done < size) {
    return;
  }
  lastDraw = millis();
  uint32_t w = (uint64_t)done * (W - 42) / size;
  tft.drawRect(20, HEAD + 84, W - 40, 20, C_DIM);
  tft.fillRect(21, HEAD + 85, w > W - 42 ? W - 42 : w, 18, C_ACCENT);
  char s[32];
  snprintf(s, sizeof s, "   %u / %u KB   ", (unsigned)(done / 1024), (unsigned)(size / 1024));
  text(s, W / 2, HEAD + 120, 2, C_DIM, MC_DATUM);
}

static void runDownload() {
  const HubPkg& p = fwhub::list[hubSel];
  page = GET;
  body();
  header(true);
  text(p.file(), W / 2, HEAD + 20, 2, C_DIM, MC_DATUM);
  text("Downloading", W / 2, HEAD + 56, 4, C_FG, MC_DATUM);
  char path[96];
  pkgPath(p, path, sizeof path);
  const char* err = SD.totalBytes() - SD.usedBytes() < p.size + 65536 ? "The SD card is full." : 0;
  if (!err) {
    err = fwhub::download(p, SD, path, getProgress);
  }
  tft.fillRect(0, HEAD + 40, W, H - HEAD - 40, C_BG);
  if (err) {
    message("Failed", err, CATALOG);
    return;
  }
  scan();
  gotIdx = -1;
  for (int i = 0; i < nFiles; i++) {
    if (!strcmp(files[i].name, p.file())) {
      gotIdx = i;
    }
  }
  text("Saved", W / 2, HEAD + 56, 4, C_OK, MC_DATUM);
  text("on the SD card, its sha256 checked", W / 2, HEAD + 92, 2, C_FG, MC_DATUM);
  button(0, 160, "BACK", C_DIM);
  button(160, 160, "INSTALL", gotIdx >= 0 ? C_OK : C_BAR);
}

static void wifiScan() {
  ble::pause(true);
  busy("Looking for Wi-Fi networks...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks(), order[64], m = 0;
  for (int i = 0; i < n && m < 64; i++) {
    if (WiFi.SSID(i).length()) {
      order[m++] = i;
    }
  }
  std::sort(order, order + m, [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });  // strongest first
  nNets = netTop = 0;
  for (int k = 0; k < m && nNets < 20; k++) {  // each name once
    String name = WiFi.SSID(order[k]);
    bool dup = false;
    for (int j = 0; j < nNets; j++) {
      dup |= name == nets[j].ssid;
    }
    if (!dup) {
      strlcpy(nets[nNets].ssid, name.c_str(), sizeof nets[nNets].ssid);
      nets[nNets++].open = WiFi.encryptionType(order[k]) == WIFI_AUTH_OPEN;
    }
  }
  WiFi.scanDelete();
  show(WIFI);
}

static void drawWifi() {
  body();
  if (!nNets) {
    text("No Wi-Fi networks", W / 2, 100, 4, C_DIM, MC_DATUM);
  }
  for (int r = 0; r < ROWS && netTop + r < nNets; r++) {
    int i = netTop + r, y = HEAD + r * ROW;
    text(nets[i].ssid, 8, y + ROW / 2, 2, !strcmp(nets[i].ssid, ssid) ? C_OK : C_FG, ML_DATUM, 240);
    if (nets[i].open) {
      text("open", W - 8, y + ROW / 2, 2, C_DIM, MR_DATUM);
    }
    tft.drawFastHLine(0, y + ROW - 1, W, C_BAR);
  }
  button(0, 64, "UP", C_DIM);
  button(64, 64, "DOWN", C_DIM);
  button(128, 96, "RESCAN", C_DIM);
  button(224, 96, "BACK", C_ACCENT);
}

// the keyboard: 4 rows of 10, in three layouts (all of printable ASCII, with SPACE)
#define KEY_Y (HEAD + 46)
#define KEY_W 32
#define KEY_H 34
static const char* LAYOUT[3] = {"1234567890qwertyuiopasdfghjkl-zxcvbnm_.@",
                                "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM_.@",
                                "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~        "};
static const char* LAYOUT_NEXT[3] = {"ABC", "#+=", "abc"};

static void drawTyped() {
  char s[70];
  snprintf(s, sizeof s, "%s_", typed);
  tft.fillRect(0, HEAD + 22, W, 22, C_BG);
  text(s, 8, HEAD + 33, 2, C_FG, ML_DATUM, W - 16);
}

static void drawKeys() {
  body();
  char s[64];
  snprintf(s, sizeof s, "Password for %s", ssid);
  text(s, 8, HEAD + 11, 2, C_DIM, ML_DATUM, W - 16);
  drawTyped();
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  for (int i = 0; i < 40; i++) {
    int x = (i % 10) * KEY_W, y = KEY_Y + (i / 10) * KEY_H;
    char c[2] = {LAYOUT[keyMode][i], 0};
    if (c[0] != ' ') {
      tft.fillRoundRect(x + 2, y + 2, KEY_W - 4, KEY_H - 4, 4, C_BAR);
      tft.setTextColor(C_FG, C_BAR);
      tft.drawString(c, x + KEY_W / 2, y + KEY_H / 2);
    }
  }
  button(0, 64, LAYOUT_NEXT[keyMode], C_DIM);
  button(64, 64, "SPACE", C_DIM);
  button(128, 64, "DEL", C_DIM);
  button(192, 64, "BACK", C_DIM);
  button(256, 64, "JOIN", C_OK);
}

static void saveWifi() {
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
}

// Wi-Fi, then the catalogue for the FM-1 on the hub (or for any FM-1)
static void goOnline() {
  if (!sdOk) {
    message("No SD card", "The packages go onto the SD card: put one in, and start again.", FILES);
    return;
  }
  if (!ssid[0]) {
    wifiScan();
    return;
  }
  ble::pause(true);
  char s[64];
  snprintf(s, sizeof s, "Joining %s...", ssid);
  busy(s);
  const char* err = fwhub::join(ssid, pass, idle);
  if (err) {
    message("No Wi-Fi", err, WIFI);
    return;
  }
  busy("Reading the Firmware Hub's catalogue...");
  int d = fm1::current();
  err = fwhub::fetch(d >= 0 ? fm1::known(d) : 0, SD);
  if (err) {
    message("No catalogue", err, FILES);
    return;
  }
  hubTop = 0;
  show(CATALOG);
}

// ------------------------------------------------------------------------------------------- files ---
static void scan() {
  nFiles = top = 0;
  if (!SD.exists(DIR)) {
    bool made = SD.mkdir(DIR);  // so there is a place for the packages next time
    Serial.printf("mkdir " DIR ": %s\n", made ? "ok" : "failed");
  }
  File dir = SD.open(DIR);
  if (!dir || !dir.isDirectory()) {
    return;
  }
  for (File f = dir.openNextFile(); f && nFiles < MAX_FILES; f = dir.openNextFile()) {
    const char* name = strrchr(f.name(), '/') ? strrchr(f.name(), '/') + 1 : f.name();
    bool skip = f.isDirectory() || name[0] == '.';
    f.close();
    if (skip) {
      continue;
    }
    strlcpy(files[nFiles].name, name, sizeof files[nFiles].name);
    char path[64];
    snprintf(path, sizeof path, DIR "/%s", name);
    Pkg pkg;
    files[nFiles].product[0] = 0;
    if (pkg.open(SD, path)) {
      strlcpy(files[nFiles].product, pkg.product, sizeof files[nFiles].product);
    }
    pkg.f.close();
    nFiles++;
  }
  qsort(files, nFiles, sizeof files[0], [](const void* a, const void* b) {
    return strcasecmp((const char*)a, (const char*)b);
  });
}

// ---------------------------------------------------------------------------------------- the loop ---
static void tap(int x, int y, bool repeat = false) {
  bool foot = y >= H - FOOT;
  if (page == RULE && !foot && y >= HEAD) {
    int col = x / 160, row = (y - HEAD) / CELL_H, lx = x - col * 160;
    int dir = lx < 50 ? -1 : lx > 110 ? 1
                                      : 0;
    if (dir && row < 4 && (!repeat || row == 1 || row == 3)) {
      learning = false;
      midimap::step(edit, col * 4 + row, dir);
      drawRule();
    }
    return;
  }
  if (repeat) {
    return;
  }
  if (page == FILES) {
    if (foot) {
      if (x < 64) {
        top = top - ROWS < 0 ? 0 : top - ROWS;
      } else if (x < 128) {
        top = top + ROWS < nFiles ? top + ROWS : top;
      } else if (x < 224) {
        goOnline();
        return;
      } else {
        show(DEVICES);
        return;
      }
      show(FILES);
    } else if (y >= HEAD) {
      int i = top + (y - HEAD) / ROW;
      if (i < nFiles && files[i].product[0]) {
        sel = i;
        show(CONFIRM);
      }
    }
  } else if (page == CONFIRM && foot) {
    int d = fm1::current();
    if (x < 160) {
      show(FILES);
    } else if (d >= 0) {
      if (fm1::known(d)->ota() || fits(d)) {
        runInstall(fm1::known(d)->ota());
      }
    }
  } else if (page == RUN && foot) {
    show(FILES);
  } else if (page == DEVICES && foot) {
    if (x < 88) {
      thru = !thru;
      prefs.putBool("thru", thru);
      hub.routeAll(thru);
      show(DEVICES);
    } else if (x < 176) {
      show(BT);
    } else if (x < 248) {
      show(MAP);
    } else {
      show(FILES);
    }
  } else if (page == MAP) {
    if (foot) {
      if (x < 64) {
        mapTop = mapTop - ROWS < 0 ? 0 : mapTop - ROWS;
      } else if (x < 128) {
        mapTop = mapTop + ROWS < midimap::count ? mapTop + ROWS : mapTop;
      } else if (x < 224) {
        if (midimap::count < MAP_RULES) {
          midimap::blank(edit);
          editIdx = -1;
          learning = false;
          show(RULE);
        }
        return;
      } else {
        show(DEVICES);
        return;
      }
      show(MAP);
    } else if (y >= HEAD) {
      int i = mapTop + (y - HEAD) / ROW;
      if (i < midimap::count) {
        edit = midimap::rules[i];
        editIdx = i;
        learning = false;
        show(RULE);
      }
    }
  } else if (page == CATALOG) {
    if (foot) {
      if (x < 64) {
        hubTop = hubTop - ROWS < 0 ? 0 : hubTop - ROWS;
      } else if (x < 128) {
        hubTop = hubTop + ROWS < fwhub::count ? hubTop + ROWS : hubTop;
      } else if (x < 224) {
        wifiScan();
        return;
      } else {
        show(FILES);
        return;
      }
      show(CATALOG);
    } else if (y >= HEAD && hubTop + (y - HEAD) / ROW < fwhub::count) {
      hubSel = hubTop + (y - HEAD) / ROW;
      show(ITEM);
    }
  } else if (page == ITEM && foot) {
    if (x < 160) {
      show(CATALOG);
    } else {
      runDownload();
    }
  } else if (page == GET && foot) {  // it was saved
    if (x < 160) {
      show(CATALOG);
    } else if (gotIdx >= 0) {
      sel = gotIdx;
      show(CONFIRM);
    }
  } else if (page == WIFI) {
    if (foot) {
      if (x < 64) {
        netTop = netTop - ROWS < 0 ? 0 : netTop - ROWS;
      } else if (x < 128) {
        netTop = netTop + ROWS < nNets ? netTop + ROWS : netTop;
      } else if (x < 224) {
        wifiScan();
        return;
      } else {
        show(FILES);
        return;
      }
      show(WIFI);
    } else if (y >= HEAD && netTop + (y - HEAD) / ROW < nNets) {
      int i = netTop + (y - HEAD) / ROW;
      bool same = !strcmp(ssid, nets[i].ssid);
      strlcpy(ssid, nets[i].ssid, sizeof ssid);
      if (nets[i].open) {
        pass[0] = 0;
        saveWifi();
        goOnline();
      } else {
        strlcpy(typed, same ? pass : "", sizeof typed);
        keyMode = 0;
        show(KEYS);
      }
    }
  } else if (page == KEYS) {
    size_t n = strlen(typed);
    if (foot) {
      if (x < 64) {
        keyMode = (keyMode + 1) % 3;
        drawKeys();
      } else if (x < 128 && n < sizeof typed - 1) {
        typed[n] = ' ';
        typed[n + 1] = 0;
        drawTyped();
      } else if (x >= 128 && x < 192 && n) {
        typed[n - 1] = 0;
        drawTyped();
      } else if (x >= 192 && x < 256) {
        show(WIFI);
      } else if (x >= 256) {
        strlcpy(pass, typed, sizeof pass);
        saveWifi();
        goOnline();
      }
    } else if (y >= KEY_Y && y < KEY_Y + 4 * KEY_H) {
      char c = LAYOUT[keyMode][(y - KEY_Y) / KEY_H * 10 + x / KEY_W];
      if (c != ' ' && n < sizeof typed - 1) {
        typed[n] = c;
        typed[n + 1] = 0;
        drawTyped();
      }
    }
  } else if (page == BT) {
    if (foot) {
      if (x < 160) {
        ble::setOn(!ble::isOn());
        prefs.putBool("bt", ble::isOn());
        drawBt();
      } else {
        show(DEVICES);
      }
    } else if (y >= HEAD + ROW) {
      int r = (y - HEAD - ROW) / ROW;
      if (r < nShown) {
        ble::toggle(shown[r]);
        drawBt();
      }
    }
  } else if (page == MSG && foot) {
    if (msgBack == WIFI) {
      wifiScan();
    } else {
      show(msgBack);
    }
  } else if (page == RULE && foot) {
    if (x < 80) {
      learning = !learning;
      learnSeq = hub.seq;
      drawRule();
    } else if (x < 160) {
      edit.outType = edit.outType == MAP_DROP ? 0 : MAP_DROP;
      drawRule();
    } else if (x < 240) {
      if (editIdx >= 0) {
        midimap::remove(editIdx);
        storeMap();
      }
      show(MAP);
    } else {
      midimap::put(editIdx, edit);
      storeMap();
      show(MAP);
    }
  }
}

// the USB serial console, for testing without hands: "shot" sends the screen (tools/shot.py), "tap X Y" taps
static void console() {
  static char line[96];
  static int n;
  int c;
  while ((c = Serial.read()) >= 0) {
    if (c != '\n' && c != '\r') {
      if (n < (int)sizeof line - 1) {
        line[n++] = (char)c;
      }
      continue;
    }
    line[n] = 0;
    n = 0;
    int x, y;
    if (!strcmp(line, "shot")) {
      static uint16_t row[W];
      Serial.printf("SHOT %d %d\n", W, H);
      for (int r = 0; r < H; r++) {
        tft.readRect(0, r, W, 1, row);
        Serial.write((const uint8_t*)row, sizeof row);
      }
    } else if (!strncmp(line, "ls", 2)) {
      File dir = SD.open(line[2] == ' ' ? line + 3 : "/");
      for (File f = dir ? dir.openNextFile() : File(); f; f = dir.openNextFile()) {
        Serial.printf("%s%s %u\n", f.name(), f.isDirectory() ? "/" : "", (unsigned)f.size());
      }
      Serial.println("ls done");
    } else if (!strncmp(line, "cat ", 4)) {
      File f = SD.open(line + 4);
      while (f && f.available()) {
        Serial.write(f.read());
      }
      Serial.println("cat done");
    } else if (!strncmp(line, "mount ", 6) && isdigit(line[6])) {  // fake the box: "mount 0 nanoKONTROL2"
      uint8_t p[40] = {0x34, 0x12, 0x78, 0x56};
      uint32_t k = 4;
      for (const char* c = line + 8; *c && k < sizeof p; c++) {
        p[k++] = (uint8_t)*c;
      }
      hub.frame(LINK_MOUNT, line[6] - '0', p, k);
    } else if (!strncmp(line, "umount ", 7)) {
      hub.frame(LINK_UMOUNT, line[7] - '0', 0, 0);
    } else if (!strncmp(line, "midi ", 5) && isdigit(line[5])) {  // "midi 0 b0 0a 40"
      uint8_t p[16];
      uint32_t k = 0;
      for (char* c = line + 6; *c && k < sizeof p;) {
        p[k++] = (uint8_t)strtol(c, &c, 16);
      }
      hub.frame(LINK_MIDI, line[5] - '0', p, k);
    } else if (!strncmp(line, "wifi ", 5)) {  // "wifi NAME PASSWORD" (a name without spaces)
      char* sp = strchr(line + 5, ' ');
      strlcpy(pass, sp ? sp + 1 : "", sizeof pass);
      if (sp) {
        *sp = 0;
      }
      strlcpy(ssid, line + 5, sizeof ssid);
      saveWifi();
      Serial.printf("wifi %s\n", ssid);
    } else if (!strcmp(line, "bt")) {
      ble::status(Serial);
    } else if (sscanf(line, "tap %d %d", &x, &y) == 2) {
      Serial.printf("tap %d %d\n", x, y);
      tap(x, y);
    }
  }
}

#ifdef CYD_S3
// the link to the box: in this board (s3box.h)
static void boxPump() {
  s3box::pump();
  ble::pump();  // Bluetooth MIDI in and out, as often
}
#else
// the link to the box: its UART, on CN1
static link_rx_t uartRx;

static void uartSend(uint8_t type, uint8_t d, const uint8_t* p, uint32_t n) {
  uint8_t wire[LINK_WIRE];
  Serial2.write(wire, link_encode(type, d, p, n, wire));
}

static void uartPump() {
  int c;
  while ((c = Serial2.read()) >= 0) {
    if (link_rx_byte(&uartRx, (uint8_t)c)) {
      hub.frame(uartRx.type, uartRx.dev, uartRx.payload, uartRx.n);
    }
  }
  ble::pump();  // Bluetooth MIDI in and out, as often as the UART
}
#endif

void setup() {
  Serial.begin(460800);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(C_BG);
  touchBegin();
  prefs.begin("fm1buddy");
  thru = prefs.getBool("thru", true);
  strlcpy(ssid, prefs.getString("ssid", "").c_str(), sizeof ssid);
  strlcpy(pass, prefs.getString("pass", "").c_str(), sizeof pass);
#ifdef CYD_S3
  hub.begin(s3box::send, boxPump);
  s3box::begin();
#else
  Serial2.setRxBufferSize(4096);
  Serial2.begin(LINK_BAUD, SERIAL_8N1, LINK_RX, LINK_TX);
  hub.begin(uartSend, uartPump);
#endif
  hub.own(ble::send);
  ble::begin(prefs.getBool("bt", true));
#ifdef CYD_S3
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
  sdOk = SD_MMC.begin("/sdcard", false);
#else
  sdSpi.begin(18, 19, 23, 5);
  sdOk = SD.begin(5, sdSpi, 20000000);
#endif
  if (sdOk) {
    scan();
    if (!SD.exists(MAP_FILE)) {
      midimap::save(SD);  // no rules yet: just how to write them
    }
    midimap::load(SD);
    File f = SD.exists(WIFI_FILE) ? SD.open(WIFI_FILE) : File();  // the Wi-Fi, from a computer: name, password
    if (f) {
      String name = f.readStringUntil('\n'), word = f.readStringUntil('\n');
      name.trim();
      word.trim();
      if (name.length()) {
        strlcpy(ssid, name.c_str(), sizeof ssid);
        strlcpy(pass, word.c_str(), sizeof pass);
        saveWifi();
      }
      f.close();
    }
  }
  header(true);
  show(FILES);
  Serial.printf("fm1buddy: SD %s (type %d, %u MB, %u MB used), %d files\n", sdOk ? "ok" : "missing", SD.cardType(),
                (unsigned)(SD.cardSize() >> 20), (unsigned)(SD.usedBytes() >> 20), nFiles);
}

void loop() {
  static bool box;
  hub.poll();
  ble::loop();
  if (ble::changed() && page == BT) {
    drawBt();
  }
  if (hub.box != box) {
    box = hub.box;
    hub.routeAll(thru);
  }
  if (hub.changed) {  // the rules name devices: the box gets them again for what is there now
    hub.changed = false;
    midimap::push();
  }
  if (page == RULE && learning && hub.seq != learnSeq) {
    midimap::learn(edit);
    learning = false;
    drawRule();
  }
  if (fm1::watch() && (page == DEVICES || page == CONFIRM)) {
    show(page);
  }
  if (page != RUN) {
    header();
  }
  int x, y;
  bool repeat;
  if (touchTap(x, y, &repeat)) {
    Serial.printf("touch %d %d%s\n", x, y, repeat ? " (held)" : "");
    tap(x, y, repeat);
  }
  console();
  delay(2);
}
