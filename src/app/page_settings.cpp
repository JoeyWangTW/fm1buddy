// Settings: Wi-Fi, the theme, the brightness, the Hub's list, and what's on the SD card
#include <stdio.h>
#include <string.h>

#include "catalog.h"
#include "hal.h"
#include "jobs.h"
#include "ui_priv.h"

using namespace ui;

static std::string ssid;  // the network the password is for
static lv_obj_t *ta, *msg, *scanBtn, *scanSpin, *nets;

static void themePage(lv_obj_t* page);
static void aboutPage(lv_obj_t* page);
static void passwordPage(lv_obj_t* page);

static void goTo(lv_event_t* e) {
  go((Builder)lv_event_get_user_data(e));
}

// a row: a name on the left, a value and > on the right
static lv_obj_t* menuRow(lv_obj_t* parent, const char* name, const char* value, Builder to) {
  const Theme& t = theme();
  lv_obj_t* r = listRow(parent, to ? goTo : 0, (void*)to);
  label(r, name, &lv_font_montserrat_16, t.text);
  lv_obj_t* v = line(r, value ? value : "", &lv_font_montserrat_14, t.dim);
  lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_flex_grow(v, 1);
  if (to) {
    label(r, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, t.dim);
  }
  return r;
}

// --------------------------------------------------------------------------------------------- the menu ---
static void brightness(lv_event_t* e) {
  lv_obj_t* s = (lv_obj_t*)lv_event_get_target(e);
  settings.brightness = (uint8_t)lv_slider_get_value(s);
  hal::backlight(settings.brightness);
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
    settingsSave();
  }
}

static void hubAuto(lv_event_t* e) {
  settings.hubAuto = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
  settingsSave();
}

void ui::settingsPage(lv_obj_t* page) {
  const Theme& t = theme();
  header(page, "Settings");
  lv_obj_t* body = scroller(page);
  menuRow(body, LV_SYMBOL_WIFI "  Wi-Fi", settings.ssid[0] ? settings.ssid : "not set", wifiPage);
  menuRow(body, LV_SYMBOL_IMAGE "  Theme", t.name, themePage);

  lv_obj_t* r = listRow(body, 0, 0);
  lv_obj_set_style_pad_right(r, 16, 0);
  lv_obj_set_style_pad_column(r, 18, 0);
  label(r, LV_SYMBOL_EYE_OPEN "  Light", &lv_font_montserrat_16, t.text);
  lv_obj_t* s = lv_slider_create(r);
  lv_slider_set_range(s, 10, 100);
  lv_slider_set_value(s, settings.brightness, LV_ANIM_OFF);
  lv_obj_set_flex_grow(s, 1);
  lv_obj_set_height(s, 8);
  lv_obj_add_event_cb(s, brightness, LV_EVENT_VALUE_CHANGED, 0);
  lv_obj_add_event_cb(s, brightness, LV_EVENT_RELEASED, 0);

  r = menuRow(body, LV_SYMBOL_REFRESH "  Update Hub list", 0, 0);
  lv_obj_t* sw = lv_switch_create(r);
  if (settings.hubAuto) {
    lv_obj_add_state(sw, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(sw, hubAuto, LV_EVENT_VALUE_CHANGED, 0);

  menuRow(body, LV_SYMBOL_SD_CARD "  About", FM1BUDDY_VERSION, aboutPage);
}

// ----------------------------------------------------------------------------------------------- Wi-Fi ---
static void scanned() {
  if (jobs::kind == jobs::SCAN) {
    if (!jobs::ok) {
      toast(jobs::error.c_str(), true);
    }
    go(wifiPage);
  }
}

static void showScan() {
  bool busy = jobs::running && jobs::kind == jobs::SCAN;
  lv_obj_add_flag(busy ? scanBtn : scanSpin, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(busy ? scanSpin : scanBtn, LV_OBJ_FLAG_HIDDEN);
}

static void startScan(lv_event_t*) {
  jobs::aps.clear();
  jobs::scan();
  showScan();
}

static void pickNet(lv_event_t* e) {
  ssid = jobs::aps[(intptr_t)lv_event_get_user_data(e)].ssid;
  go(passwordPage);
}

static void forget(lv_event_t*) {
  settings.ssid[0] = settings.pass[0] = 0;
  settingsSave();
  go(wifiPage);
}

void ui::wifiPage(lv_obj_t* page) {
  const Theme& t = theme();
  lv_obj_t* head = header(page, "Wi-Fi", settingsPage);
  scanBtn = button(head, LV_SYMBOL_REFRESH, startScan);
  lv_obj_set_size(scanBtn, 40, 30);
  scanSpin = lv_spinner_create(head);
  lv_obj_set_size(scanSpin, 26, 26);
  lv_obj_set_style_arc_width(scanSpin, 4, 0);
  lv_obj_set_style_arc_width(scanSpin, 4, LV_PART_INDICATOR);
  lv_obj_set_style_margin_hor(scanSpin, 7, 0);

  nets = scroller(page);
  if (settings.ssid[0]) {
    lv_obj_t* r = listRow(nets, 0, 0);
    lv_obj_t* l = line(r, (std::string(LV_SYMBOL_OK "  ") + settings.ssid).c_str(), &lv_font_montserrat_16, t.accent);
    lv_obj_set_flex_grow(l, 1);
    lv_obj_t* b = button(r, "Forget", forget);
    lv_obj_set_height(b, 30);
  }
  for (size_t i = 0; i < jobs::aps.size(); i++) {
    const hal::Ap& a = jobs::aps[i];
    if (a.ssid == settings.ssid) {
      continue;
    }
    lv_obj_t* r = listRow(nets, pickNet, (void*)(intptr_t)i);
    lv_obj_t* l = line(r, a.ssid.c_str(), &lv_font_montserrat_16, t.text);
    lv_obj_set_flex_grow(l, 1);
    char s[24];
    snprintf(s, sizeof s, "%s%d dBm", a.open ? "open  " : "", a.rssi);
    label(r, s, &lv_font_montserrat_12, a.rssi > -60 ? t.text : t.dim);
  }
  if (jobs::aps.empty()) {
    lv_obj_t* l = label(nets, "Tap " LV_SYMBOL_REFRESH " to look for networks. Wi-Fi is only on while the Hub's list or a firmware downloads.",
                        &lv_font_montserrat_14, t.dim);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_pad_all(l, 8, 0);
  }
  showScan();
  tick = showScan;
  jobDone = scanned;
}

static void joined() {
  if (jobs::kind != jobs::JOIN) {
    return;
  }
  if (jobs::ok) {
    strncpy(settings.ssid, ssid.c_str(), sizeof settings.ssid - 1);
    strncpy(settings.pass, lv_textarea_get_text(ta), sizeof settings.pass - 1);
    settingsSave();
    toast("Joined, and saved.");
    go(wifiPage);
  } else {
    lv_label_set_text(msg, jobs::error.c_str());
    lv_obj_set_style_text_color(msg, color(theme().knob[0]), 0);
  }
}

static void showJoin() {
  if (jobs::running && jobs::kind == jobs::JOIN) {
    lv_label_set_text(msg, jobs::status().c_str());
    lv_obj_set_style_text_color(msg, color(theme().dim), 0);
  }
}

static void keys(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CANCEL) {
    go(wifiPage);
  } else if (!jobs::join(ssid.c_str(), lv_textarea_get_text(ta))) {
    toast("Busy: try again in a moment.", true);
  }
}

static void reveal(lv_event_t* e) {
  bool hide = !lv_textarea_get_password_mode(ta);
  lv_textarea_set_password_mode(ta, hide);
  lv_label_set_text(lv_obj_get_child((lv_obj_t*)lv_event_get_target(e), 0), hide ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
}

static void passwordPage(lv_obj_t* page) {
  const Theme& t = theme();
  fullscreen(true);
  header(page, ssid.c_str(), wifiPage);
  lv_obj_t* r = box(page, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(r, 4, 0);
  ta = lv_textarea_create(r);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_password_mode(ta, true);
  lv_textarea_set_placeholder_text(ta, "password");
  lv_obj_set_flex_grow(ta, 1);
  lv_obj_add_state(ta, LV_STATE_FOCUSED);
  if (ssid == settings.ssid) {
    lv_textarea_set_text(ta, settings.pass);
  }
  lv_obj_t* eye = button(r, LV_SYMBOL_EYE_OPEN, reveal);
  lv_obj_set_size(eye, 40, 36);
  msg = line(page, "", &lv_font_montserrat_12, t.dim);
  lv_obj_set_width(msg, LV_PCT(100));
  lv_obj_t* kb = lv_keyboard_create(page);
  lv_obj_set_size(kb, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(kb, 1);
  lv_keyboard_set_textarea(kb, ta);
  lv_obj_add_event_cb(kb, keys, LV_EVENT_READY, 0);
  lv_obj_add_event_cb(kb, keys, LV_EVENT_CANCEL, 0);
  tick = showJoin;
  jobDone = joined;
}

// ----------------------------------------------------------------------------------------------- theme ---
static void pickTheme(lv_event_t* e) {
  settings.theme = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  settingsSave();
  rebuild();
}

static void themePage(lv_obj_t* page) {
  header(page, "Theme", settingsPage);
  lv_obj_t* grid = scroller(page);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(grid, 6, 0);
  lv_obj_set_style_pad_all(grid, 2, 0);
  for (int i = 0; i < THEME_COUNT; i++) {
    const Theme& t = THEMES[i];
    lv_obj_t* c = box(grid);
    lv_obj_set_size(c, (PAGE_W - 12 - 4 - 12) / 3, 54);
    lv_obj_set_style_bg_color(c, color(t.bg), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 6, 0);
    lv_obj_set_style_border_width(c, i == settings.theme ? 3 : 1, 0);
    lv_obj_set_style_border_color(c, color(i == settings.theme ? theme().accent : t.beat), 0);
    lv_obj_set_style_pad_all(c, 6, 0);
    lv_obj_set_style_pad_row(c, 4, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, pickTheme, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    label(c, t.name, &lv_font_montserrat_12, t.text);
    lv_obj_t* bars = box(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bars, 3, 0);
    for (uint32_t col : {t.accent, t.knob[0], t.knob[1], t.knob[2], t.knob[3]}) {
      lv_obj_t* b = box(bars);
      lv_obj_set_size(b, 10, 10);
      lv_obj_set_style_radius(b, 5, 0);
      lv_obj_set_style_bg_color(b, color(col), 0);
      lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    }
  }
}

// ----------------------------------------------------------------------------------------------- about ---
static void fact(lv_obj_t* parent, const char* name, const std::string& value) {
  const Theme& t = theme();
  lv_obj_t* r = box(parent, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_ver(r, 4, 0);
  lv_obj_t* n = label(r, name, &lv_font_montserrat_14, t.dim);
  lv_obj_set_width(n, 90);
  lv_obj_t* v = label(r, value.c_str(), &lv_font_montserrat_14, t.text);
  lv_label_set_long_mode(v, LV_LABEL_LONG_WRAP);
  lv_obj_set_flex_grow(v, 1);
}

static void aboutPage(lv_obj_t* page) {
  header(page, "About", settingsPage);
  lv_obj_t* body = scroller(page);
  fact(body, "fm1buddy", FM1BUDDY_VERSION);
  uint64_t total, used;
  char s[48];
  if (hal::sd() && hal::sdSpace(total, used)) {
    snprintf(s, sizeof s, "%.1f of %.1f GB free", (total - used) / 1e9, total / 1e9);
    fact(body, "SD card", s);
  } else {
    fact(body, "SD card", "none");
  }
  fact(body, "Firmwares", std::to_string(catalog::sd.size()) + " in /fm1");
  fact(body, "Hub list", catalog::hub.empty() ? "not yet" : std::to_string(catalog::hub.size()) + ", from " + catalog::hubUpdated);
  fact(body, "Hub", "thefirmwarehub.com");
}
