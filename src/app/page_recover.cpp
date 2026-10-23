// Recovery (from Settings): an FM-1 whose firmware doesn't start, back to one that does. Its chip's boot loader (in its
// ROM: uboot.h) is woken by a key on the USB lines as it's switched on, so it has to be straight on fm1buddy's port,
// not on the hub; the board restarts into recovery mode for it (hal.h recoveryModeSet), and out again when it's done
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <algorithm>

#include "catalog.h"
#include "hal.h"
#include "jobs.h"
#include "uboot.h"
#include "ui_priv.h"

using namespace ui;

static lv_obj_t* wrapped(lv_obj_t* body, const char* text, const lv_font_t* font, uint32_t color) {
  lv_obj_t* l = label(body, text, font, color);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, LV_PCT(100));
  return l;
}

// "/fm1/x.fwsc" -> "x.fwsc"
static std::string nameOf(const std::string& p) {
  size_t s = p.rfind('/');
  return s == std::string::npos ? p : p.substr(s + 1);
}

// --------------------------------------------------------------------------------- recovery mode ---
static lv_obj_t *status, *cancelBtn;
static bool started;
static int shownStep, shownKey;
static bool shownRunning;

static void doneTapped(lv_event_t*) {
  hal::recoveryModeSet(false);  // (the board restarts, as it was)
  started = false;
  go(SETTINGS, settingsPage);
}

static void againTapped(lv_event_t*) {
  started = false;
  hal::recoveryModeSet(true);  // (the board restarts into it again)
  go(recoverPage);
}

static void recoverTick() {
  if (jobs::running != shownRunning || jobs::step != shownStep || hal::keyState() != shownKey) {
    go(recoverPage);
    return;
  }
  if (status && jobs::running) {
    lv_label_set_text(status, jobs::status().c_str());
  }
}

void ui::recoverPage(lv_obj_t* page) {
  const Theme& t = theme();
  fullscreen(true);
  if (!started) {
    started = jobs::recover();
  }
  shownRunning = jobs::running, shownStep = jobs::step, shownKey = hal::keyState();
  bool mine = jobs::kind == jobs::RECOVER;
  header(page, LV_SYMBOL_WARNING "  Recovering the FM-1");
  lv_obj_t* body = scroller(page);
  lv_obj_set_style_pad_row(body, 6, 0);
  std::string from = jobs::recovering(), name = nameOf(from);
  for (const Firmware& f : catalog::sd) {
    if (f.file == name && !f.name.empty()) {
      name = f.name;
    }
  }
  wrapped(body, ("Putting " + name + " on it.").c_str(), &lv_font_montserrat_14, t.text);
  bool waiting = jobs::running && (jobs::step == jobs::GETTING_READY || jobs::step == jobs::KEYING);
  if (jobs::running) {
    status = wrapped(body, jobs::status().c_str(), &lv_font_montserrat_16, t.accent);
    if (waiting && hal::keyState() < hal::KEY_ANSWERED) {
      wrapped(body,
              "It has to be straight on fm1buddy's USB-C adapter, with nothing between (no hub), and the charger in the "
              "adapter. If it's on already, switch it off, then on.",
              &lv_font_montserrat_12, t.dim);
    } else if (!waiting) {
      wrapped(body, "Don't unplug it or switch it off until this is done.", &lv_font_montserrat_12, t.dim);
    }
  } else if (mine && jobs::ok) {
    status = 0;
    wrapped(body, jobs::error.empty() ? "Done: it's on the FM-1." : jobs::error.c_str(), &lv_font_montserrat_16, t.accent);
    wrapped(body,
            "Switch the FM-1 off and on again: it starts what was written. Then unplug it, plug the hub back in, and "
            "tap Done.",
            &lv_font_montserrat_14, t.text);
  } else {
    status = 0;
    wrapped(body, mine ? jobs::error.c_str() : "Busy: try again.", &lv_font_montserrat_14, t.knob[0]);
  }
  if (!jobs::saved.empty() && mine) {
    wrapped(body, ("Its flash as it was is on the SD card: " BACKUPS_DIR "/" + jobs::saved + ".").c_str(),
            &lv_font_montserrat_12, t.dim);
  }
  lv_obj_t* row = box(body, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_top(row, 6, 0);
  lv_obj_set_style_pad_column(row, 8, 0);
  if (jobs::running) {
    // only while nothing is going on it yet
    cancelBtn = button(row, LV_SYMBOL_CLOSE " Cancel", doneTapped);
    if (!waiting) {
      lv_obj_add_state(cancelBtn, LV_STATE_DISABLED);
    }
  } else {
    if (!(mine && jobs::ok)) {
      button(row, LV_SYMBOL_REFRESH " Try again", againTapped);
    }
    button(row, LV_SYMBOL_OK " Done", doneTapped);
  }
  wrapped(body, "Done and Cancel restart fm1buddy as it was, with the hub.", &lv_font_montserrat_12, t.dim);
  tick = recoverTick;
}

// ------------------------------------------------------------------------------- from Settings ---
struct Choice {
  std::string path, name, what;
};
static std::vector<Choice> choices;
static std::string chosen;

// the backups' .bin files, newest first, with the firmware each says it was taken from
static void backups(std::vector<Choice>& out) {
  std::string dir = catalog::path(BACKUPS_DIR);
  std::vector<Choice> found;
  if (DIR* d = dir.empty() ? 0 : opendir(dir.c_str())) {
    while (dirent* e = readdir(d)) {
      size_t n = strlen(e->d_name);
      if (n <= 4 || strcasecmp(e->d_name + n - 4, ".bin")) {
        continue;
      }
      Choice c{BACKUPS_DIR "/" + std::string(e->d_name), e->d_name, "backup"};
      std::string txt = dir + "/" + std::string(e->d_name, n - 4) + ".txt";
      if (FILE* f = fopen(txt.c_str(), "r")) {
        char line[96];
        while (fgets(line, sizeof line, f)) {
          if (!strncmp(line, "firmware ", 9)) {
            line[strcspn(line, "\r\n")] = 0;
            c.what = line + 9;  // (what it was taken from)
          }
        }
        fclose(f);
      }
      found.push_back(c);
    }
    closedir(d);
  }
  std::sort(found.begin(), found.end(), [](const Choice& a, const Choice& b) {
    long x = atol(a.name.c_str() + 4), y = atol(b.name.c_str() + 4);
    return x != y ? x > y : a.name > b.name;
  });
  out.insert(out.end(), found.begin(), found.end());
}

static void chooseTapped(lv_event_t* e) {
  chosen = choices[(intptr_t)lv_event_get_user_data(e)].path;
  go(recoverStartPage);
}

static void startTapped(lv_event_t*) {
  if (!jobs::prepare(chosen)) {
    toast("Busy: try again in a moment.", true);
  }
  go(recoverStartPage);
}

static void prepared() {
  if (jobs::kind == jobs::PREPARE && !jobs::ok) {
    toast(jobs::error.c_str(), true);
  }
  go(hal::recoveryMode() ? recoverPage : recoverStartPage);  // (the board restarts into it: the simulator doesn't)
}

static lv_obj_t* prepStatus;

static void startTick() {
  if (prepStatus) {
    lv_label_set_text(prepStatus, jobs::running ? jobs::status().c_str() : "");
  }
}

void ui::recoverStartPage(lv_obj_t* page) {
  const Theme& t = theme();
  header(page, "Recover the FM-1", settingsPage);
  lv_obj_t* body = scroller(page);
  lv_obj_set_style_pad_row(body, 4, 0);
  wrapped(body,
          "For an FM-1 that doesn't start any more. Its chip has a boot loader no firmware can break: fm1buddy wakes "
          "it as the FM-1 is switched on, saves its flash as it was to the SD card, and writes a firmware or a backup "
          "back onto it (the part the chip starts from is never written).",
          &lv_font_montserrat_12, t.text);

  // what goes on it
  label(body, "1. What goes on it", &lv_font_montserrat_14, t.text);
  choices.clear();
  for (const Firmware& f : catalog::sd) {
    if (!strncmp(f.product.c_str(), "FM-1_", 5)) {
      choices.push_back({FW_DIR "/" + f.file, f.name.empty() ? f.file : f.name,
                         f.stock ? "stock" : f.version.empty() ? "firmware"
                                                               : f.version});
    }
  }
  backups(choices);
  if (std::none_of(choices.begin(), choices.end(), [](const Choice& c) { return c.path == chosen; })) {
    chosen.clear();
  }
  for (size_t i = 0; i < choices.size(); i++) {
    const Choice& c = choices[i];
    bool on = c.path == chosen;
    lv_obj_t* r = listRow(body, chooseTapped, (void*)(intptr_t)i);
    label(r, on ? LV_SYMBOL_OK : " ", &lv_font_montserrat_14, t.accent);
    lv_obj_t* n = line(r, c.name.c_str(), &lv_font_montserrat_14, on ? t.accent : t.text);
    lv_obj_set_flex_grow(n, 1);
    lv_obj_t* w = line(r, c.what.c_str(), &lv_font_montserrat_12, t.dim);
    lv_obj_set_width(w, 80);
    lv_obj_set_style_text_align(w, LV_TEXT_ALIGN_RIGHT, 0);
  }
  if (choices.empty()) {
    wrapped(body,
            hal::sd() ? "Nothing on the SD card: download a firmware (stock V15 is on the Hub) or make a backup first."
                      : "No SD card.",
            &lv_font_montserrat_12, t.knob[0]);
  }

  // then
  lv_obj_t* h = label(body, "2. Then", &lv_font_montserrat_14, t.text);
  lv_obj_set_style_pad_top(h, 6, 0);
  static const char* STEPS[] = {
      "Switch the FM-1 off.",
      "Unplug the hub from fm1buddy's USB-C adapter, and plug the FM-1 in instead: straight in, nothing between. Keep "
      "the charger in the adapter: it powers fm1buddy (the FM-1 can't).",
      "Tap Start: fm1buddy restarts, and waits for it.",
      "Switch the FM-1 on when it says so. Leave it plugged in and on until it's done.",
  };
  for (int i = 0; i < 4; i++) {
    char s[200];
    snprintf(s, sizeof s, "%d. %s", i + 1, STEPS[i]);
    wrapped(body, s, &lv_font_montserrat_12, t.text);
  }
  lv_obj_t* row = box(body, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_top(row, 6, 0);
  lv_obj_t* b = button(row, LV_SYMBOL_PLAY " Start", startTapped);
  if (chosen.empty() || jobs::running || !hal::sd()) {
    lv_obj_add_state(b, LV_STATE_DISABLED);
  }
  prepStatus = label(body, jobs::running ? jobs::status().c_str() : "", &lv_font_montserrat_14, t.accent);
  wrapped(body,
          uboot::haveLoader() ? "Cancel on the next screen, before it's switched on, to come back."
                              : "Start downloads JieLi's loader for its boot loader first (Wi-Fi), once.",
          &lv_font_montserrat_12, t.dim);
  tick = startTick;
  jobDone = prepared;
}
