// MIDI: what is plugged in, and Bluetooth MIDI (off until it's turned on here). The USB host and Bluetooth come
// later: for now this is the page's shape.
#include "ui_priv.h"

using namespace ui;

static bool bluetooth;  // never saved: off after power-on

static void toggleBt(lv_event_t* e) {
  bluetooth = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
}

void ui::midiPage(lv_obj_t* page) {
  const Theme& t = theme();
  header(page, "MIDI");
  lv_obj_t* body = scroller(page);
  lv_obj_set_style_pad_row(body, 6, 0);

  label(body, "USB DEVICES", &lv_font_montserrat_12, t.dim);
  lv_obj_t* none = label(body, "Nothing plugged in. Plug the FM-1 and a controller into the hub.", &lv_font_montserrat_14, t.dim);
  lv_label_set_long_mode(none, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(none, LV_PCT(100));

  label(body, "BLUETOOTH", &lv_font_montserrat_12, t.dim);
  lv_obj_t* r = listRow(body, 0, 0);
  lv_obj_t* l = label(r, LV_SYMBOL_BLUETOOTH "  Bluetooth MIDI", &lv_font_montserrat_16, t.text);
  lv_obj_set_flex_grow(l, 1);
  lv_obj_t* sw = lv_switch_create(r);
  if (bluetooth) {
    lv_obj_add_state(sw, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(sw, toggleBt, LV_EVENT_VALUE_CHANGED, 0);
  lv_obj_t* note = label(body, "Phones, tablets and Bluetooth keyboards join the USB devices. Off again after power-off.",
                         &lv_font_montserrat_12, t.dim);
  lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(note, LV_PCT(100));
}
