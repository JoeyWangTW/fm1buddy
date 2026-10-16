// fm1buddy: pick a firmware from SD:/fm1 and install it on the FM-1, through the USB MIDI host box
#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>

#include "fm1ota.h"
#include "link.h"
#include "midimap.h"
#include "touch.h"

#define DIR "/fm1"
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
static SPIClass sdSpi(VSPI);
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
            RULE };
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
    strlcpy(s, "no host box", sizeof s);
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
  button(128, 96, "RESCAN", C_DIM);
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
    text("Plug the FM-1 into the host box's hub.", 8, HEAD + 120, 2, C_DIM);
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

static void drawDevices() {
  body();
  int y = HEAD + 6;
  for (int d = 0; d < LINK_DEVS; d++) {
    if (!hub.dev[d].on) {
      continue;
    }
    char s[24];
    snprintf(s, sizeof s, "%04X:%04X", hub.dev[d].vid, hub.dev[d].pid);
    text(hub.dev[d].name[0] ? hub.dev[d].name : "(no name)", 8, y + 10, 2, C_FG, ML_DATUM, 190);
    text(s, W - 8, y + 10, 2, C_DIM, MR_DATUM);
    if (fm1::known(d)) {
      text(fm1::known(d)->text, 8, y + 28, 2, C_OK);
    }
    y += 42;
  }
  if (!hub.box) {
    text("Host box not connected (CN1)", W / 2, 100, 2, C_BAD, MC_DATUM);
  } else if (y == HEAD + 6) {
    text("Nothing plugged into the host box", W / 2, 100, 2, C_DIM, MC_DATUM);
  }
  button(0, 120, thru ? "MIDI THRU: ON" : "MIDI THRU: OFF", thru ? C_OK : C_DIM);
  button(120, 100, "MAP", C_DIM);
  button(220, 100, "FILES", C_ACCENT);
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

static void show(Page p) {
  page = p;
  if (p == FILES) {
    drawFiles();
  } else if (p == CONFIRM) {
    drawConfirm();
  } else if (p == DEVICES) {
    drawDevices();
  } else if (p == MAP) {
    drawMap();
  } else if (p == RULE) {
    drawRule();
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
        scan();
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
    if (x < 120) {
      thru = !thru;
      prefs.putBool("thru", thru);
      hub.routeAll(thru);
      show(DEVICES);
    } else if (x < 220) {
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
    } else if (sscanf(line, "tap %d %d", &x, &y) == 2) {
      Serial.printf("tap %d %d\n", x, y);
      tap(x, y);
    }
  }
}

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
}

void setup() {
  Serial.begin(460800);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(C_BG);
  touchBegin();
  prefs.begin("fm1buddy");
  thru = prefs.getBool("thru", true);
  Serial2.setRxBufferSize(4096);
  Serial2.begin(LINK_BAUD, SERIAL_8N1, LINK_RX, LINK_TX);
  hub.begin(uartSend, uartPump);
  sdSpi.begin(18, 19, 23, 5);
  sdOk = SD.begin(5, sdSpi, 20000000);
  if (sdOk) {
    scan();
    if (!SD.exists(MAP_FILE)) {
      midimap::save(SD);  // no rules yet: just how to write them
    }
    midimap::load(SD);
  }
  header(true);
  show(FILES);
  Serial.printf("fm1buddy: SD %s (type %d, %u MB, %u MB used), %d files\n", sdOk ? "ok" : "missing", SD.cardType(),
                (unsigned)(SD.cardSize() >> 20), (unsigned)(SD.usedBytes() >> 20), nFiles);
}

void loop() {
  static bool box;
  hub.poll();
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
