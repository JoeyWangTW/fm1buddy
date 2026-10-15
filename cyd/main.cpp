// fm1buddy: pick a firmware from SD:/fx1 and install it on the FM-1, through the USB MIDI host box
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>

#include "fm1ota.h"
#include "link.h"
#include "touch.h"

#define DIR "/fx1"
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
            DEVICES };
static Page page;
static bool thru = true;  // MIDI between the devices on the hub

// what each device on the hub answered (FM-1s only), by its mount
static struct {
  uint32_t serial;  // the mount it was asked on (0: not yet)
  uint32_t askAt;
  bool ok;
  Fm1Id id;
} ids[LINK_DEVS];

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

static int fm1Dev() {  // the device that answered as an FM-1 (or its loader), -1 if none
  for (int d = 0; d < LINK_DEVS; d++) {
    if (hub.dev[d].on && ids[d].ok && ids[d].serial == hub.dev[d].serial) {
      return d;
    }
  }
  return -1;
}

static bool fits(int d) {  // the selected package is for device d's model
  Pkg pkg;
  strlcpy(pkg.product, files[sel].product, sizeof pkg.product);
  return pkg.fits(ids[d].id);
}

static char shownStatus[48];

static void header(bool force = false) {
  char s[48];
  int d = fm1Dev();
  if (!hub.box) {
    strlcpy(s, "no host box", sizeof s);
  } else if (d < 0) {
    strlcpy(s, "no FM-1", sizeof s);
  } else {
    strlcpy(s, ids[d].id.text, sizeof s);
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
  int d = fm1Dev();
  text(files[sel].name, 8, HEAD + 20, 4, C_FG, ML_DATUM, W - 16);
  text("package", 8, HEAD + 54, 2, C_DIM);
  text(files[sel].product, 100, HEAD + 54, 2, C_ACCENT);
  text("device", 8, HEAD + 78, 2, C_DIM);
  text(d < 0 ? "none" : ids[d].id.text, 100, HEAD + 78, 2, d < 0 ? C_BAD : C_OK);
  const char* label = "INSTALL";
  bool can = d >= 0;
  if (d < 0) {
    text("Plug the FM-1 into the host box's hub.", 8, HEAD + 120, 2, C_DIM);
  } else if (ids[d].id.ota()) {
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
    if (ids[d].ok && ids[d].serial == hub.dev[d].serial) {
      text(ids[d].id.text, 8, y + 28, 2, C_OK);
    }
    y += 42;
  }
  if (!hub.box) {
    text("Host box not connected (CN1)", W / 2, 100, 2, C_BAD, MC_DATUM);
  } else if (y == HEAD + 6) {
    text("Nothing plugged into the host box", W / 2, 100, 2, C_DIM, MC_DATUM);
  }
  button(0, 160, thru ? "MIDI THRU: ON" : "MIDI THRU: OFF", thru ? C_OK : C_DIM);
  button(160, 160, "FILES", C_ACCENT);
}

static void show(Page p) {
  page = p;
  if (p == FILES) {
    drawFiles();
  } else if (p == CONFIRM) {
    drawConfirm();
  } else if (p == DEVICES) {
    drawDevices();
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
  // the FM-1 restarted: ask it again
  memset(ids, 0, sizeof ids);
  button(0, W, "OK", C_ACCENT);
}

// ------------------------------------------------------------------------------------------- files ---
static void scan() {
  nFiles = top = 0;
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
// ask new devices that look like an FM-1 who they are, once they have settled
static bool probe() {
  bool changed = false;
  for (int d = 0; d < LINK_DEVS; d++) {
    if (!hub.dev[d].on) {
      if (ids[d].serial) {
        ids[d].serial = 0;
        ids[d].ok = false;
        changed = true;
      }
      continue;
    }
    if (ids[d].serial == hub.dev[d].serial) {
      continue;
    }
    if (!ids[d].askAt) {
      ids[d].askAt = millis();
      changed = true;  // a new device: the list changes
    } else if (millis() - ids[d].askAt > 1500) {
      ids[d].serial = hub.dev[d].serial;
      ids[d].askAt = 0;
      ids[d].ok = fm1::candidate(d) && fm1::handshake(d, ids[d].id);
      changed = true;
    }
  }
  return changed;
}

static void tap(int x, int y) {
  bool foot = y >= H - FOOT;
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
    int d = fm1Dev();
    if (x < 160) {
      show(FILES);
    } else if (d >= 0) {
      if (ids[d].id.ota() || fits(d)) {
        runInstall(ids[d].id.ota());
      }
    }
  } else if (page == RUN && foot) {
    show(FILES);
  } else if (page == DEVICES && foot) {
    if (x < 160) {
      thru = !thru;
      hub.routeAll(thru);
      show(DEVICES);
    } else {
      show(FILES);
    }
  }
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(C_BG);
  touchBegin();
  hub.begin();
  sdSpi.begin(18, 19, 23, 5);
  sdOk = SD.begin(5, sdSpi, 20000000);
  if (sdOk) {
    scan();
  }
  header(true);
  show(FILES);
}

void loop() {
  static bool box;
  hub.poll();
  if (hub.box != box) {
    box = hub.box;
    hub.routeAll(thru);
  }
  if (probe() && (page == DEVICES || page == CONFIRM)) {
    show(page);
  }
  if (page != RUN) {
    header();
  }
  int x, y;
  if (touchTap(x, y)) {
    tap(x, y);
  }
  delay(2);
}
