// The USB-C port as the USB host, for MIDI devices on a hub (through a USB-C OTG adapter with a charging port):
// TinyUSB's host (its hub and MIDI drivers), in a task of its own, handing the packets to midi.h.
//
// A USB storage device (the FM-1's boot loader, uboot.h) gets no driver: when one is plugged in, its bulk endpoints are
// opened here, and the app's SCSI commands (storageCommand) run in this task, one at a time, as Bulk-Only Transport.
//
// The S3's USB has 8 channels. TinyUSB takes one only while a transfer is going: each device's waiting MIDI IN holds
// one, the rest (control, MIDI OUT, the hub's status) come and go. So about 5 or 6 MIDI devices on a hub, not 2.
#include <Arduino.h>
#include <esp_private/periph_ctrl.h>
#include <esp_private/usb_phy.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>

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

// ------------------------------------------------------------------------------------- storage ---
static struct {
  uint8_t daddr;  // 0: none
  uint16_t vid, pid;
  uint8_t in, out;  // its bulk endpoints (0: not open yet)
  bool pending;     // mounted: its endpoints to open (in the task)
  uint32_t tag;
} stor;

// a command from a job, run by the task: 0 none, 1 asked, 2 done
static struct {
  std::atomic<int> state{0};
  const uint8_t* cdb;
  uint8_t cdbLen;
  bool in;
  uint8_t* buf;
  uint32_t len, got, ms;
  bool ok;
} cmd;

static volatile xfer_result_t xres;
static volatile uint32_t xlen;
static void xferDone(tuh_xfer_t* x) {
  xlen = x->actual_len;
  xres = x->result;
}

// one bulk transfer, waited for (running TinyUSB meanwhile)
static bool xferSync(uint8_t ep, void* buf, uint16_t len, uint32_t* got, uint32_t ms) {
  uint8_t daddr = stor.daddr;
  xres = XFER_RESULT_INVALID;
  xlen = 0;
  tuh_xfer_t x = {};
  x.daddr = daddr;
  x.ep_addr = ep;
  x.buflen = len;
  x.buffer = (uint8_t*)buf;
  x.complete_cb = xferDone;
  if (!tuh_edpt_xfer(&x)) {
    return false;
  }
  for (uint32_t start = millis(); xres == XFER_RESULT_INVALID;) {
    tuh_task_ext(1, false);
    if (stor.daddr != daddr) {
      return false;  // it went
    }
    if (millis() - start > ms) {
      tuh_edpt_abort_xfer(daddr, ep);
      return false;
    }
  }
  if (got) {
    *got = xlen;
  }
  return xres == XFER_RESULT_SUCCESS;
}

// its endpoints, from its configuration
static void storageOpen() {
  static uint8_t desc[256];
  stor.pending = false;
  if (tuh_descriptor_get_configuration_sync(stor.daddr, 0, desc, sizeof desc) != XFER_RESULT_SUCCESS) {
    hal::log("usb: storage %u: no configuration", stor.daddr);
    return;
  }
  uint16_t total = desc[2] | desc[3] << 8;
  bool msc = false;
  for (uint16_t o = 0; o + 2 <= total && o + 2 <= sizeof desc && desc[o]; o += desc[o]) {
    if (desc[o + 1] == TUSB_DESC_INTERFACE) {
      msc = desc[o + 5] == TUSB_CLASS_MSC;
    } else if (desc[o + 1] == TUSB_DESC_ENDPOINT && msc && (desc[o + 3] & 3) == TUSB_XFER_BULK) {
      const tusb_desc_endpoint_t* ep = (const tusb_desc_endpoint_t*)&desc[o];
      if (tuh_edpt_open(stor.daddr, ep)) {
        (ep->bEndpointAddress & 0x80 ? stor.in : stor.out) = ep->bEndpointAddress;
      }
    }
  }
  hal::log("usb: storage %u (%04X:%04X): bulk in %02X, out %02X", stor.daddr, stor.vid, stor.pid, stor.in, stor.out);
}

// one SCSI command, Bulk-Only Transport: its CBW, its data, its CSW
static bool storageRun() {
  static uint8_t cbw[31], csw[13], data[512];
  if (!stor.daddr || !stor.in || !stor.out || cmd.len > sizeof data) {
    return false;
  }
  uint32_t tag = ++stor.tag;
  memset(cbw, 0, sizeof cbw);
  memcpy(cbw, "USBC", 4);
  memcpy(cbw + 4, &tag, 4);
  memcpy(cbw + 8, &cmd.len, 4);
  cbw[12] = cmd.in ? 0x80 : 0;
  cbw[14] = cmd.cdbLen;
  memcpy(cbw + 15, cmd.cdb, cmd.cdbLen);
  if (!xferSync(stor.out, cbw, sizeof cbw, 0, cmd.ms)) {
    return false;
  }
  if (cmd.len) {
    if (!cmd.in) {
      memcpy(data, cmd.buf, cmd.len);
    }
    if (!xferSync(cmd.in ? stor.in : stor.out, data, cmd.len, &cmd.got, cmd.ms)) {
      return false;
    }
    if (cmd.in) {
      memcpy(cmd.buf, data, cmd.got);
    }
  }
  uint32_t n = 0;
  if (!xferSync(stor.in, csw, sizeof csw, &n, cmd.ms) || n != 13 || memcmp(csw, "USBS", 4) || memcmp(csw + 4, &tag, 4)) {
    return false;
  }
  return csw[12] == 0;
}

bool hal::storageThere(uint16_t vid, uint16_t pid) {
  return stor.daddr && stor.in && stor.out && stor.vid == vid && stor.pid == pid;
}

bool hal::storageCommand(const uint8_t* cdb, uint8_t cdbLen, bool in, uint8_t* buf, uint32_t len, uint32_t* got,
                         uint32_t ms) {
  cmd.cdb = cdb, cmd.cdbLen = cdbLen > 16 ? 16 : cdbLen, cmd.in = in, cmd.buf = buf, cmd.len = len, cmd.got = 0, cmd.ms = ms, cmd.ok = false;
  cmd.state = 1;
  for (uint32_t start = millis(); cmd.state != 2; delay(1)) {
    if (millis() - start > ms * 3 + 1000) {
      return false;  // (the task is stuck: it shouldn't be)
    }
  }
  cmd.state = 0;
  if (got) {
    *got = cmd.got;
  }
  return cmd.ok;
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
  if (vid == 0x4C4A && pid == 0x8057) {  // the FM-1's boot loader (the only storage device asked for)
    stor.daddr = daddr, stor.vid = vid, stor.pid = pid, stor.in = stor.out = 0, stor.pending = true;
  }
}

void tuh_umount_cb(uint8_t daddr) {
  hal::log("usb: device %u went", daddr);
  if (daddr == stor.daddr) {
    stor.daddr = 0;
  }
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
    if (stor.pending && stor.daddr) {
      storageOpen();
    }
    if (cmd.state == 1) {
      cmd.ok = storageRun();
      cmd.state = 2;
    }
    midi::service();
    ledLoop();
  }
}

void board::usbHostBegin() {
  xTaskCreatePinnedToCore(hostTask, "usbhost", 8192, 0, 5, 0, 1);
}
