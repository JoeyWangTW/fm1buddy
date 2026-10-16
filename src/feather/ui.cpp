#include "ui.h"

#include <Arduino.h>
#include <FatFS.h>
#include <SDFS.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "fm1ota.h"
#include "link.h"
#include "midimap.h"

extern bool sdOk, thru;
void saveThru();

#if defined(OLED_SSD1306)
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0);
#elif defined(OLED_SH1106)
static U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0);
#else
static U8G2_SH1107_64X128_F_HW_I2C oled(U8G2_R1);  // the Adafruit 128x64 OLED FeatherWing
#endif

#ifndef BTN_BACK
#define BTN_BACK -1
#endif

#define W 128
#define LH 10                  // a line
#define ROWS ((64 - LH) / LH)  // under the title
#define COLS (W / 6)

// ------------------------------------------------------------------------------------------- buttons ---
enum Key { NONE,
           UP,
           DOWN,
           OK,
           BACK };

struct Btn {
  int pin;
  bool down, longDone;
  uint32_t at, rep;
};
static Btn bUp = {BTN_UP}, bDown = {BTN_DOWN}, bOk = {BTN_OK}, bBack = {BTN_BACK};

static bool pressed(int pin) {
  return pin >= 0 && !digitalRead(pin);
}

// on press, then again and again while held
static bool repeating(Btn& b) {
  uint32_t now = millis();
  if (!pressed(b.pin)) {
    b.down = false;
    return false;
  }
  if (!b.down) {
    b.down = true;
    b.at = b.rep = now;
    return true;
  }
  if (now - b.at > 400 && now - b.rep > 80) {
    b.rep = now;
    return true;
  }
  return false;
}

static bool press(Btn& b) {
  bool d = pressed(b.pin), was = b.down;
  b.down = d;
  return d && !was;
}

static Key key() {
  static uint32_t last;
  uint32_t now = millis();
  if (now - last < 10) {  // debounce
    return NONE;
  }
  last = now;
  if (repeating(bUp)) {
    return UP;
  }
  if (repeating(bDown)) {
    return DOWN;
  }
  if (BTN_BACK >= 0) {
    if (press(bBack)) {
      return BACK;
    }
    return press(bOk) ? OK : NONE;
  }
  // three buttons: OK when it's let go, back when it's held
  bool d = pressed(bOk.pin);
  if (d && !bOk.down) {
    bOk.down = true;
    bOk.at = now;
    bOk.longDone = false;
  } else if (d && !bOk.longDone && now - bOk.at > 600) {
    bOk.longDone = true;
    return BACK;
  } else if (!d && bOk.down) {
    bOk.down = false;
    if (!bOk.longDone) {
      return OK;
    }
  }
  return NONE;
}

// ------------------------------------------------------------------------------------------- drawing ---
static void fit(char* s, int cols) {
  if ((int)strlen(s) > cols) {
    s[cols - 1] = '~';
    s[cols] = 0;
  }
}

static const char* status() {
  int d = fm1::current();
  return !hub.box ? "no box" : d < 0 ? ""
                                     : fm1::known(d)->text;
}

static void title(const char* t) {
  oled.clearBuffer();
  oled.setDrawColor(1);
  oled.drawBox(0, 0, W, LH);
  oled.setDrawColor(0);
  oled.drawStr(2, 8, t);
  const char* s = status();
  oled.drawStr(W - 2 - oled.getStrWidth(s), 8, s);
  oled.setDrawColor(1);
}

// one line under the title: left, and right-aligned right (it wins the room)
static void line(int i, const char* left, const char* right = "", bool sel = false) {
  char l[COLS + 1], r[COLS + 1];
  int y = LH + i * LH + 1;
  strlcpy(r, right, sizeof r);
  int rc = strlen(r), room = COLS - (rc ? rc + 1 : 0);
  strlcpy(l, left, sizeof l);
  fit(l, room < 1 ? 1 : room);
  if (sel) {
    oled.drawBox(0, y, W, LH);
    oled.setDrawColor(0);
  }
  oled.drawStr(1, y + 8, l);
  oled.drawStr(W - 1 - oled.getStrWidth(r), y + 8, r);
  oled.setDrawColor(1);
}

// a list: n items, the selected one in view
struct List {
  int sel, top, n;
  void move(int d) {
    if (!n) {
      return;
    }
    sel = (sel + d + n) % n;
    if (sel < top) {
      top = sel;
    }
    if (sel >= top + ROWS) {
      top = sel - ROWS + 1;
    }
  }
};

// --------------------------------------------------------------------------------------------- state ---
enum Page { MENU,
            FILES,
            CONFIRM,
            RESULT,
            DEVICES,
            MAP,
            RULE };
static Page page;
static bool dirty = true;
static List menu, files, rules, rule;

#define MAX_FILES 48
static struct {
  char name[40];
  char product[24];  // "": not an FM-1 package
  bool sd;
} file[MAX_FILES];
static int nFiles;

static MapRule edit;
static int editIdx;  // -1: a new rule
static bool editing, learning;
static uint32_t learnSeq;
static char result[96];
static bool resultOk;

static void scan(fs::FS& fs, bool sd) {
  Dir d = fs.openDir("/fm1");
  while (d.next() && nFiles < MAX_FILES) {
    String name = d.fileName();
    if (d.isDirectory() || name[0] == '.') {
      continue;
    }
    strlcpy(file[nFiles].name, name.c_str(), sizeof file[nFiles].name);
    file[nFiles].sd = sd;
    char path[64];
    snprintf(path, sizeof path, "/fm1/%s", file[nFiles].name);
    Pkg pkg;
    file[nFiles].product[0] = 0;
    if (pkg.open(fs, path)) {
      strlcpy(file[nFiles].product, pkg.product, sizeof file[nFiles].product);
    }
    pkg.f.close();
    nFiles++;
  }
}

static void scanAll() {
  nFiles = 0;
  scan(FatFS, false);
  if (sdOk) {
    scan(SDFS, true);
  }
  qsort(file, nFiles, sizeof file[0], [](const void* a, const void* b) {
    return strcasecmp((const char*)a, (const char*)b);
  });
  files.n = nFiles;
  if (files.sel >= nFiles) {
    files.sel = files.top = 0;
  }
}

static bool fits(int d) {  // the selected package is for device d's model
  Pkg pkg;
  strlcpy(pkg.product, file[files.sel].product, sizeof pkg.product);
  return pkg.fits(*fm1::known(d));
}

// ----------------------------------------------------------------------------------------- the pages ---
static const char* RULE_ROWS[] = {"from", "ch", "type", "", "-> to", "-> ch", "-> type", ""};
#define RULE_N 12  // the 8 fields, learn, drop, delete, save

static void draw() {
  char b[COLS + 8], v[COLS + 8];
  switch (page) {
    case MENU:
      title("fm1buddy");
      line(0, "Install firmware", "", menu.sel == 0);
      line(1, "Devices", "", menu.sel == 1);
      line(2, "MIDI map", "", menu.sel == 2);
      line(3, "MIDI thru", thru ? "on" : "off", menu.sel == 3);
      break;
    case FILES:
      title("Firmware");
      if (!nFiles) {
        line(1, "Nothing in fm1/:");
        line(2, "copy .fwsc files to");
        line(3, "the fm1buddy drive");
      }
      for (int r = 0; r < ROWS && files.top + r < nFiles; r++) {
        int i = files.top + r;
        line(r, file[i].name, file[i].product[0] ? file[i].product : "--", i == files.sel);
      }
      break;
    case CONFIRM: {
      int d = fm1::current();
      const Fm1Id* id = d < 0 ? 0 : fm1::known(d);
      title("Install");
      line(0, file[files.sel].name);
      line(1, "package", file[files.sel].product);
      line(2, "FM-1", id ? id->text : "none");
      line(4, !id ? "plug the FM-1 in" : id->ota() ? "OK: finish install"
                                     : fits(d)     ? "OK: install"
                                                   : "another model",
           "", id && (id->ota() || fits(d)));
      break;
    }
    case RESULT: {
      title(resultOk ? "Installed" : "Failed");
      // the message, broken at spaces
      const char* s = result;
      for (int r = 0; r < ROWS && *s; r++) {
        int n = strlen(s) > COLS ? COLS : strlen(s);
        if (s[n]) {
          int cut = n;
          while (cut > 0 && s[cut] != ' ') {
            cut--;
          }
          if (cut) {
            n = cut;
          }
        }
        memcpy(b, s, n);
        b[n] = 0;
        line(r, b);
        s += n;
        while (*s == ' ') {
          s++;
        }
      }
      break;
    }
    case DEVICES: {
      title("Devices");
      int r = 0;
      for (int d = 0; d < LINK_DEVS; d++) {
        if (hub.dev[d].on && r < ROWS) {
          const Fm1Id* id = fm1::known(d);
          line(r++, hub.dev[d].name[0] ? hub.dev[d].name : "(no name)", id ? id->text : "");
        }
      }
      if (!r) {
        line(1, hub.box ? "nothing plugged in" : "no USB host");
      }
      break;
    }
    case MAP:
      title("MIDI map");
      rules.n = midimap::count + 1;
      for (int r = 0; r < ROWS && rules.top + r < rules.n; r++) {
        int i = rules.top + r;
        if (i < midimap::count) {
          midimap::format(midimap::rules[i], b, sizeof b);
        } else {
          strlcpy(b, "+ add a rule", sizeof b);
        }
        line(r, b, "", i == rules.sel);
      }
      break;
    case RULE:
      title(editIdx < 0 ? "New rule" : "Rule");
      for (int r = 0; r < ROWS && rule.top + r < RULE_N; r++) {
        int i = rule.top + r;
        const char* left = "";
        const char* right = "";
        if (i < 8) {
          left = RULE_ROWS[i];
          if (i == 3 || i == 7) {  // "note", "program" or "number", lowercase
            snprintf(b, sizeof b, "%s%s", i == 7 ? "-> " : "", midimap::label(edit, i));
            for (char* c = b; *c; c++) {
              *c = (char)tolower(*c);
            }
            left = b;
          }
          right = i == 4 && edit.outType == MAP_DROP ? "drop" : midimap::show(edit, i, v, sizeof v);
          if (editing && i == rule.sel) {
            static char e[COLS + 4];
            snprintf(e, sizeof e, "<%s>", right);
            right = e;
          }
        } else if (i == 8) {
          left = learning ? "learn: play something" : "learn";
        } else if (i == 9) {
          left = "drop it";
          right = edit.outType == MAP_DROP ? "yes" : "no";
        } else if (i == 10) {
          left = editIdx < 0 ? "cancel" : "delete";
        } else {
          left = "save";
        }
        line(r, left, right, i == rule.sel);
      }
      break;
  }
  oled.sendBuffer();
  dirty = false;
}

static void go(Page p) {
  page = p;
  editing = learning = false;
  dirty = true;
}

// ---------------------------------------------------------------------------------------- installing ---
static uint32_t lastDraw;
static uint32_t pkgSize;

static void progress(Fm1Step step, uint32_t served, uint32_t addr) {
  static const char* NAMES[] = {"starting", "checking", "starting the loader", "writing", "restarting", "done"};
  if ((step == STEP_VERIFY || step == STEP_WRITE) && millis() - lastDraw < 200) {
    return;
  }
  lastDraw = millis();
  char b[24];
  title("Installing");
  line(0, file[files.sel].name);
  line(1, NAMES[step]);
  if (step == STEP_WRITE && pkgSize) {
    uint32_t w = (uint64_t)(addr + 1) * (W - 4) / pkgSize;
    oled.drawFrame(0, LH * 3 + 2, W, 8);
    oled.drawBox(2, LH * 3 + 4, w > W - 4 ? W - 4 : w, 4);
  }
  if (served) {
    snprintf(b, sizeof b, "%u", (unsigned)served);
    line(4, "keep it plugged in", b);
  } else {
    line(4, "keep it plugged in");
  }
  oled.sendBuffer();
}

static void install(bool resume) {
  static Pkg pkg;
  char path[64];
  snprintf(path, sizeof path, "/fm1/%s", file[files.sel].name);
  const char* err = pkg.open(file[files.sel].sd ? SDFS : FatFS, path) ? 0 : "Can't read the package.";
  pkgSize = pkg.size;
  progress(STEP_START, 0, 0);
  if (!err) {
    err = resume ? fm1::resume(pkg, progress) : fm1::install(pkg, progress);
  }
  pkg.f.close();
  hub.routeAll(thru);
  resultOk = !err;
  strlcpy(result, err ? err : file[files.sel].product, sizeof result);
  fm1::forget();  // the FM-1 restarted: ask it again
  go(RESULT);
}

// -------------------------------------------------------------------------------------------- keys ---
static void ruleKey(Key k) {
  if (editing) {
    if (k == UP || k == DOWN) {
      midimap::step(edit, rule.sel, k == UP ? 1 : -1);
    } else {
      editing = false;
    }
    return;
  }
  if (k == UP || k == DOWN) {
    rule.move(k == UP ? -1 : 1);
    learning = false;
  } else if (k == BACK) {
    go(MAP);
  } else if (k == OK) {
    if (rule.sel < 8) {
      editing = midimap::active(edit, rule.sel);
    } else if (rule.sel == 8) {
      learning = !learning;
      learnSeq = hub.seq;
    } else if (rule.sel == 9) {
      edit.outType = edit.outType == MAP_DROP ? 0 : MAP_DROP;
    } else {
      if (rule.sel == 10) {
        midimap::remove(editIdx);
      } else {
        midimap::put(editIdx, edit);
      }
      if (rule.sel == 11 || editIdx >= 0) {
        midimap::save(FatFS);
        midimap::push();
      }
      go(MAP);
    }
  }
}

static void onKey(Key k) {
  dirty = true;
  int d = fm1::current();
  switch (page) {
    case MENU:
      if (k == UP || k == DOWN) {
        menu.move(k == UP ? -1 : 1);
      } else if (k == OK) {
        if (menu.sel == 0) {
          scanAll();
          go(FILES);
        } else if (menu.sel == 1) {
          go(DEVICES);
        } else if (menu.sel == 2) {
          go(MAP);
        } else {
          thru = !thru;
          saveThru();
          hub.routeAll(thru);
        }
      }
      break;
    case FILES:
      if (k == UP || k == DOWN) {
        files.move(k == UP ? -1 : 1);
      } else if (k == OK && nFiles && file[files.sel].product[0]) {
        go(CONFIRM);
      } else if (k == BACK) {
        go(MENU);
      }
      break;
    case CONFIRM:
      if (k == OK && d >= 0 && (fm1::known(d)->ota() || fits(d))) {
        install(fm1::known(d)->ota());
      } else if (k == BACK) {
        go(FILES);
      }
      break;
    case RESULT:
      if (k == OK || k == BACK) {
        go(FILES);
      }
      break;
    case DEVICES:
      if (k == BACK || k == OK) {
        go(MENU);
      }
      break;
    case MAP:
      if (k == UP || k == DOWN) {
        rules.move(k == UP ? -1 : 1);
      } else if (k == BACK) {
        go(MENU);
      } else if (k == OK) {
        editIdx = rules.sel < midimap::count ? rules.sel : -1;
        if (editIdx >= 0) {
          edit = midimap::rules[editIdx];
        } else if (midimap::count < MAP_RULES) {
          midimap::blank(edit);
        } else {
          break;  // full
        }
        rule.sel = rule.top = 0;
        rule.n = RULE_N;
        go(RULE);
      }
      break;
    case RULE:
      ruleKey(k);
      break;
  }
}

// ----------------------------------------------------------------------------------------------- run ---
bool uiBegin() {
  Wire.begin();
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() != 0) {
    return false;  // no screen
  }
  for (int pin : {BTN_UP, BTN_OK, BTN_DOWN, BTN_BACK}) {
    if (pin >= 0) {
      pinMode(pin, INPUT_PULLUP);
    }
  }
  oled.setBusClock(400000);
  oled.begin();
  oled.setFont(u8g2_font_6x10_tf);
  menu.n = 4;
  scanAll();
  draw();
  return true;
}

void uiDriveChanged() {
  scanAll();
  dirty = true;
}

void uiLoop() {
  static char shown[28];
  if (fm1::watch() || strcmp(shown, status())) {
    strlcpy(shown, status(), sizeof shown);
    dirty = true;
  }
  if (page == RULE && learning && hub.seq != learnSeq) {
    midimap::learn(edit);
    learning = false;
    dirty = true;
  }
  Key k = key();
  if (k != NONE) {
    onKey(k);
  }
  if (dirty) {
    draw();
  }
}
