// The USB-C port as the USB host, for MIDI devices on a hub (through a USB-C OTG adapter with a charging port):
// TinyUSB's host (its hub and MIDI drivers), in a task of its own, handing the packets to midi.h.
//
// The S3's USB has 8 channels. TinyUSB takes one only while a transfer is going: each device's waiting MIDI IN holds
// one, the rest (control, MIDI OUT, the hub's status) come and go. So about 5 or 6 MIDI devices on a hub, not 2.
#include <Arduino.h>
#include <esp_private/periph_ctrl.h>
#include <esp_private/usb_phy.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board.h"
#include "hal.h"
#include "midi.h"
#include "tusb.h"

static bool hostOn;
static uint32_t ledOffAt;

static struct {
  bool on;           // midi.h knows it
  uint8_t daddr;     // its USB address
  uint16_t str[32];  // its product string, as it comes (UTF-16LE, after a 2-byte header)
} dev[MIDI_USB];

static void blink() {
#ifdef LED_RGB
  if (!ledOffAt) {
    rgbLedWrite(LED_RGB, 0, 16, 16);
  }
  ledOffAt = millis() + 20;
#endif
}

static void ledLoop() {
#ifdef LED_RGB
  if (ledOffAt && (int32_t)(millis() - ledOffAt) >= 0) {
    rgbLedWrite(LED_RGB, 0, 0, 0);
    ledOffAt = 0;
  }
#endif
}

// ------------------------------------------------------------------------------------- TinyUSB's ---
// device i is ready, with its name (or none)
static void mountMidi(uint8_t i, bool named) {
  char name[32] = "";
  if (named) {
    int n = (dev[i].str[0] & 0xFF) / 2 - 1;  // bLength, in 16-bit units, without the header
    int k = 0;
    for (int c = 1; c <= n && c < 32 && k < (int)sizeof name - 1; c++) {
      uint16_t ch = dev[i].str[c];
      name[k++] = ch >= 0x20 && ch < 0x7F ? (char)ch : '?';
    }
    name[k] = 0;
  }
  uint16_t vid = 0, pid = 0;
  tuh_vid_pid_get(dev[i].daddr, &vid, &pid);
  dev[i].on = true;
  midi::mount(i, vid, pid, name);
}

static void gotName(tuh_xfer_t* x) {
  uint8_t i = (uint8_t)x->user_data;
  if (i < MIDI_USB && tuh_midi_mounted(i) && !dev[i].on) {
    mountMidi(i, x->result == XFER_RESULT_SUCCESS);
  }
}

extern "C" {
void tuh_mount_cb(uint8_t daddr) {
  uint16_t vid = 0, pid = 0;
  tuh_vid_pid_get(daddr, &vid, &pid);
  hal::log("usb: device %u: %04X:%04X", daddr, vid, pid);
}

void tuh_umount_cb(uint8_t daddr) {
  hal::log("usb: device %u went", daddr);
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t* m) {
  if (idx >= MIDI_USB) {
    return;
  }
  dev[idx].on = false;
  dev[idx].daddr = m->daddr;
  memset(dev[idx].str, 0, sizeof dev[idx].str);
  // its name first (the FM-1 is known by it), then midi.h hears of it
  if (!tuh_descriptor_get_product_string(m->daddr, 0x0409, dev[idx].str, sizeof dev[idx].str, gotName, idx)) {
    mountMidi(idx, false);
  }
}

void tuh_midi_umount_cb(uint8_t idx) {
  if (idx < MIDI_USB && dev[idx].on) {
    dev[idx].on = false;
    midi::umount(idx);
  }
}

void tuh_midi_rx_cb(uint8_t idx, uint32_t) {
  if (idx >= MIDI_USB) {
    return;
  }
  uint8_t buf[64];
  for (uint32_t n; (n = tuh_midi_packet_read_n(idx, buf, sizeof buf));) {
    for (uint32_t k = 0; k + 4 <= n; k += 4) {
      if (buf[k] & 0x0F) {  // CIN 0: nothing (padding)
        midi::in(idx, buf + k);
      }
    }
    blink();
  }
}
}

// ------------------------------------------------------------------------------------------- hal.h ---
// from midi::service, in the host task
static uint32_t usbOut(int d, const uint8_t* p, uint32_t n) {
  if (!tuh_midi_mounted(d) || !tuh_midi_get_tx_cable_count(d)) {
    return n;  // nowhere to go
  }
  uint32_t took = tuh_midi_packet_write_n(d, p, n * 4) / 4;
  tuh_midi_write_flush(d);
  if (took) {
    blink();
  }
  return took;
}

uint32_t hal::midiOut(int d, const uint8_t* p, uint32_t n) {
  return d < MIDI_USB ? usbOut(d, p, n) : board::bleOut(d, p, n);
}

bool hal::usbHost() {
  return hostOn;
}

// ---------------------------------------------------------------------------------------------- run ---
static void hostTask(void*) {
  // the USB controller's clock, and the PHY to it as a host (TinyUSB leaves both to ESP-IDF)
  periph_module_reset(PERIPH_USB_MODULE);
  periph_module_enable(PERIPH_USB_MODULE);
  usb_phy_config_t pc = {};
  pc.controller = USB_PHY_CTRL_OTG;
  pc.target = USB_PHY_TARGET_INT;
  pc.otg_mode = USB_OTG_MODE_HOST;
  pc.otg_speed = USB_PHY_SPEED_UNDEFINED;
  usb_phy_handle_t phy;
  esp_err_t e = usb_new_phy(&pc, &phy);
  if (e != ESP_OK) {
    hal::log("usb: no PHY (%s)", esp_err_to_name(e));
    vTaskDelete(0);
  }
  if (!tuh_init(BOARD_TUH_RHPORT)) {  // its interrupt lands on this core
    hal::log("usb: TinyUSB didn't start");
    vTaskDelete(0);
  }
  hostOn = true;
  hal::log("usb: host");
  for (;;) {
    tuh_task_ext(1, false);
    midi::service();
    ledLoop();
  }
}

void board::usbHostBegin() {
  xTaskCreatePinnedToCore(hostTask, "usbhost", 8192, 0, 5, 0, 1);
}
