// MIDI: what is plugged into the hub (everything any of them plays goes to all the others), and Bluetooth MIDI (off
// until it's turned on here; then on after power-on too)
#include <stdio.h>

#include "fm1.h"
#include "hal.h"
#include "midi.h"
#include "ui_priv.h"

using namespace ui;

static uint32_t shownSeq;
static lv_obj_t *dots[MIDI_DEVS], *lasts[MIDI_DEVS];
static uint32_t counts[MIDI_DEVS], shownDropped;
static lv_obj_t* dropLabel;

// "note C4  vel 100  ch 1   (20)": the last message, and how many it has sent
static void lastText(const midi::Dev& v, char* s, int n) {
  if (!v.count) {
    snprintf(s, n, "nothing yet");
    return;
  }
  char m[40];
  midi::describe(v.last, m, sizeof m);
  snprintf(s, n, "%s   (%u)", m, (unsigned)v.count);
}

static void toggleBt(lv_event_t* e) {
  bool on = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
  hal::bleOn(on);
  hal::save("bluetooth", &on, 1);  // the same after power-on
  go(midiPage);
}

static void tapPeer(lv_event_t* e) {
  hal::bleToggle((int)(intptr_t)lv_event_get_user_data(e));
  go(midiPage);
}

static void live() {
  if (midi::seq != shownSeq || hal::bleChanged()) {
    go(midiPage);  // a device came or went, or said who it is
    return;
  }
  const Theme& t = theme();
  uint32_t now = hal::millis();
  for (int d = 0; d < MIDI_DEVS; d++) {
    midi::Dev v;
    if (!dots[d] || !midi::dev(d, v)) {
      continue;
    }
    lv_obj_set_style_text_color(dots[d], color(v.lastIn && now - v.lastIn < 150 ? t.accent : t.off), 0);
    if (v.count != counts[d]) {
      counts[d] = v.count;
      char s[56];
      lastText(v, s, sizeof s);
      lv_label_set_text(lasts[d], s);
    }
  }
  if (dropLabel && midi::dropped != shownDropped) {
    shownDropped = midi::dropped;
    lv_label_set_text_fmt(dropLabel, "%u messages dropped (a device too slow to take them)", (unsigned)shownDropped);
    lv_obj_remove_flag(dropLabel, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui::midiPage(lv_obj_t* page) {
  const Theme& t = theme();
  header(page, "MIDI");
  lv_obj_t* body = scroller(page);
  lv_obj_set_style_pad_row(body, 4, 0);
  shownSeq = midi::seq;

  int n = 0;
  for (int d = 0; d < MIDI_DEVS; d++) {
    dots[d] = lasts[d] = 0;
    midi::Dev v;
    if (!midi::dev(d, v)) {
      continue;
    }
    n++;
    counts[d] = v.count;
    lv_obj_t* r = listRow(body, 0, 0);
    dots[d] = label(r, LV_SYMBOL_BULLET, &lv_font_montserrat_20, t.off);
    lv_obj_t* col = box(r);
    lv_obj_set_flex_grow(col, 1);
    const Fm1Id* id = fm1::known(d);
    std::string name = v.name;
    if (id) {
      name += id->ota() ? "  (in update mode)" : std::string("  ") + id->text;
    }
    lv_obj_t* l = line(col, name.c_str(), &lv_font_montserrat_16, id ? t.accent : t.text);
    lv_obj_set_width(l, LV_PCT(100));
    char s[56];
    lastText(v, s, sizeof s);
    lasts[d] = line(col, s, &lv_font_montserrat_12, t.dim);
    lv_obj_set_width(lasts[d], LV_PCT(100));
  }
  if (!n) {
    const char* msg = hal::usbHost() ? "Nothing on the hub. Plug in the FM-1 and a controller: whatever one plays, the others hear."
                                     : "A computer is on the USB port, so it's a serial port for now. Plug the board into the hub "
                                       "(through the OTG adapter) to use MIDI.";
    lv_obj_t* none = label(body, msg, &lv_font_montserrat_14, t.dim);
    lv_label_set_long_mode(none, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(none, LV_PCT(100));
    lv_obj_set_style_pad_ver(none, 6, 0);
  } else if (!midi::thru) {
    label(body, "Installing: MIDI waits until it's done.", &lv_font_montserrat_12, t.knob[1]);
  }
  shownDropped = midi::dropped;
  dropLabel = line(body, "", &lv_font_montserrat_12, t.knob[0]);
  lv_obj_set_width(dropLabel, LV_PCT(100));
  if (shownDropped) {
    lv_label_set_text_fmt(dropLabel, "%u messages dropped (a device too slow to take them)", (unsigned)shownDropped);
  } else {
    lv_obj_add_flag(dropLabel, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_t* r = listRow(body, 0, 0);
  lv_obj_t* l = label(r, LV_SYMBOL_BLUETOOTH "  Bluetooth MIDI", &lv_font_montserrat_16, t.text);
  lv_obj_set_flex_grow(l, 1);
  lv_obj_t* sw = lv_switch_create(r);
  bool bt = hal::bleIsOn();
  if (bt) {
    lv_obj_add_state(sw, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(sw, toggleBt, LV_EVENT_VALUE_CHANGED, 0);
  const char* about = bt ? "Phones, tablets and computers can connect to \"fm1buddy\". Keyboards and synths around show "
                           "here: tap one to connect it (it's remembered). Off while Wi-Fi is on."
                         : "Phones, tablets and Bluetooth keyboards join the devices on the hub.";
  lv_obj_t* note = label(body, about, &lv_font_montserrat_12, t.dim);
  lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(note, LV_PCT(100));
  if (bt) {
    hal::BlePeer peers[6];
    int np = hal::blePeers(peers, 6);
    for (int i = 0; i < np; i++) {
      lv_obj_t* pr = listRow(body, tapPeer, (void*)(intptr_t)i);
      lv_obj_t* pl = line(pr, peers[i].name, &lv_font_montserrat_16, peers[i].state == 2 ? t.accent : t.text);
      lv_obj_set_flex_grow(pl, 1);
      static const char* STATES[] = {"tap to connect", "connecting...", "connected"};
      label(pr, peers[i].saved && !peers[i].state ? "not around" : STATES[peers[i].state], &lv_font_montserrat_12, t.dim);
    }
    if (!np) {
      label(body, "Looking for keyboards and synths...", &lv_font_montserrat_12, t.dim);
    }
    hal::bleSearch(true);
  }
  tick = live;
}
