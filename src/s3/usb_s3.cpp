// The USB-C port as the USB host, for MIDI devices on a hub (through a USB-C OTG adapter with a charging port): the
// ESP-IDF USB host (with its hub driver) and a small USB MIDI driver, in a task of their own. It claims each device's
// MIDI Streaming interface, reads and writes its endpoints, and hands the packets to midi.h.
//
// The S3's USB has 8 channels: the hub takes 2, and each device 3 (its control endpoint, MIDI in, MIDI out). So the
// FM-1 and one controller on the hub; a third device doesn't fit.
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <usb/usb_host.h>

#include "board.h"
#include "hal.h"
#include "midi.h"

#define RX_N 128  // packets kept from each device
#define TX_N 256  // bytes (64 packets) going to each device

static usb_host_client_handle_t client;
static bool hostOn;
static uint32_t ledOffAt;

enum { FREE,
       OPEN,
       CLOSING };

static struct Dev {
  uint8_t state;
  usb_device_handle_t h;
  uint8_t iface, epIn, epOut;
  uint16_t mpsIn;
  usb_transfer_t *in, *out;
  bool inBusy, outBusy;
  uint32_t inRetryAt;  // an IN transfer that failed: submitted again then
  uint8_t rx[RX_N][4];
  volatile uint32_t rw, rr;
  uint8_t tx[TX_N];
  uint32_t txn;
} dev[MIDI_USB];

static uint8_t newAddr[8], nNew;  // devices that came, opened in the loop (not in the event callback)
static bool seen[128];            // addresses already looked at (opened, or not MIDI)

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

// ------------------------------------------------------------------------------------ USB transfers ---
static void inDone(usb_transfer_t* t) {
  Dev& v = dev[(uintptr_t)t->context];
  v.inBusy = false;
  if (t->status == USB_TRANSFER_STATUS_COMPLETED) {
    for (int i = 0; i + 4 <= t->actual_num_bytes; i += 4) {
      const uint8_t* p = t->data_buffer + i;
      if ((p[0] & 0x0F) && v.rw - v.rr < RX_N) {  // CIN 0: nothing (padding)
        memcpy(v.rx[v.rw % RX_N], p, 4);
        v.rw++;
      }
    }
  } else if (t->status == USB_TRANSFER_STATUS_NO_DEVICE || t->status == USB_TRANSFER_STATUS_CANCELED) {
    return;
  }
  if (v.state != OPEN) {
    return;
  }
  if (t->status == USB_TRANSFER_STATUS_COMPLETED) {
    t->num_bytes = v.mpsIn;
    v.inBusy = usb_host_transfer_submit(t) == ESP_OK;
  } else {
    v.inRetryAt = millis() + 100;  // an error: again in a moment
  }
}

static void outDone(usb_transfer_t* t) {
  dev[(uintptr_t)t->context].outBusy = false;
}

static void flush(Dev& v) {
  if (v.state != OPEN || v.outBusy || !v.txn || !v.out) {
    return;
  }
  memcpy(v.out->data_buffer, v.tx, v.txn);
  v.out->num_bytes = v.txn;
  v.txn = 0;
  v.outBusy = usb_host_transfer_submit(v.out) == ESP_OK;
}

static uint32_t usbOut(int d, const uint8_t* p, uint32_t n) {
  Dev& v = dev[d];
  if (v.state != OPEN || !v.epOut) {
    return n;  // nowhere to go
  }
  if (v.outBusy) {
    return 0;  // the last ones are still going
  }
  uint32_t k = 0;
  while (k < n && v.txn + 4 <= TX_N) {
    memcpy(v.tx + v.txn, p + k * 4, 4);
    v.txn += 4;
    k++;
  }
  flush(v);
  if (k) {
    blink();
  }
  return k;
}

uint32_t hal::midiOut(int d, const uint8_t* p, uint32_t n) {
  return d < MIDI_USB ? usbOut(d, p, n) : board::bleOut(d, p, n);
}

bool hal::usbHost() {
  return hostOn;
}

// ------------------------------------------------------------------------------ devices coming, going ---
// the first MIDI Streaming interface (audio class, subclass 3) with endpoints, and its endpoints
static bool findMidi(const usb_config_desc_t* cfg, Dev& v, uint8_t& alt) {
  int off = 0;
  const usb_standard_desc_t* d = (const usb_standard_desc_t*)cfg;
  bool in = false;
  while ((d = usb_parse_next_descriptor(d, cfg->wTotalLength, &off))) {
    if (d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      if (in && (v.epIn || v.epOut)) {
        return true;
      }
      const usb_intf_desc_t* f = (const usb_intf_desc_t*)d;
      in = f->bInterfaceClass == USB_CLASS_AUDIO && f->bInterfaceSubClass == 3;
      v.iface = f->bInterfaceNumber;
      alt = f->bAlternateSetting;
      v.epIn = v.epOut = 0;
    } else if (in && d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
      const usb_ep_desc_t* e = (const usb_ep_desc_t*)d;
      usb_transfer_type_t t = USB_EP_DESC_GET_XFERTYPE(e);
      if (t != USB_TRANSFER_TYPE_BULK && t != USB_TRANSFER_TYPE_INTR) {
        continue;
      }
      if (USB_EP_DESC_GET_EP_DIR(e) && !v.epIn) {
        v.epIn = e->bEndpointAddress;
        v.mpsIn = USB_EP_DESC_GET_MPS(e);
      } else if (!USB_EP_DESC_GET_EP_DIR(e) && !v.epOut) {
        v.epOut = e->bEndpointAddress;
      }
    }
  }
  return in && (v.epIn || v.epOut);
}

// a string descriptor (UTF-16LE) as ASCII
static void ascii(const usb_str_desc_t* s, char* out, size_t n) {
  size_t k = 0;
  if (s) {
    for (int i = 0; i < (s->bLength - 2) / 2 && k < n - 1; i++) {
      uint16_t c = s->wData[i];
      out[k++] = c >= 0x20 && c < 0x7F ? (char)c : '?';
    }
  }
  out[k] = 0;
}

static void closeDev(Dev& v) {
  if (v.in) {
    usb_host_transfer_free(v.in);
  }
  if (v.out) {
    usb_host_transfer_free(v.out);
  }
  v.in = v.out = 0;
  usb_host_interface_release(client, v.h, v.iface);
  usb_host_device_close(client, v.h);
  v.state = FREE;
}

static void openDev(uint8_t addr) {
  int i = 0;
  while (i < MIDI_USB && dev[i].state != FREE) {
    i++;
  }
  if (i == MIDI_USB) {
    hal::log("usb: device %u: no room (%d devices)", addr, MIDI_USB);
    return;
  }
  Dev& v = dev[i];
  memset(&v, 0, sizeof v);
  if (seen[addr & 127]) {
    return;
  }
  seen[addr & 127] = true;
  esp_err_t oe = usb_host_device_open(client, addr, &v.h);
  if (oe != ESP_OK) {
    hal::log("usb: device %u: can't open it (%s)", addr, esp_err_to_name(oe));
    return;
  }
  const usb_device_desc_t* dd;
  const usb_config_desc_t* cfg;
  usb_device_info_t info;
  uint8_t alt = 0;
  if (usb_host_get_device_descriptor(v.h, &dd) != ESP_OK || usb_host_get_active_config_descriptor(v.h, &cfg) != ESP_OK ||
      usb_host_device_info(v.h, &info) != ESP_OK) {
    usb_host_device_close(client, v.h);
    return;
  }
  char name[32];
  ascii(info.str_desc_product, name, sizeof name);
  hal::log("usb: device %u: %04X:%04X \"%s\", class %u, %s speed", addr, dd->idVendor, dd->idProduct, name,
           dd->bDeviceClass, info.speed == USB_SPEED_LOW ? "low" : info.speed == USB_SPEED_FULL ? "full"
                                                                                                : "high");
  if (dd->bDeviceClass == USB_CLASS_HUB || !findMidi(cfg, v, alt)) {
    hal::log("usb: %04X:%04X isn't MIDI%s", dd->idVendor, dd->idProduct, dd->bDeviceClass == USB_CLASS_HUB ? " (a hub)" : "");
    usb_host_device_close(client, v.h);  // not MIDI (a hub is the hub driver's)
    return;
  }
  esp_err_t e = usb_host_interface_claim(client, v.h, v.iface, alt);
  if (e != ESP_OK) {
    // usually: the S3's USB channels are all taken
    hal::log("usb: %04X:%04X: can't claim its MIDI interface (%s)", dd->idVendor, dd->idProduct, esp_err_to_name(e));
    usb_host_device_close(client, v.h);
    return;
  }
  v.state = OPEN;
  if (v.epIn && usb_host_transfer_alloc(v.mpsIn, 0, &v.in) == ESP_OK) {
    v.in->device_handle = v.h;
    v.in->bEndpointAddress = v.epIn;
    v.in->callback = inDone;
    v.in->context = (void*)(uintptr_t)i;
    v.in->num_bytes = v.mpsIn;
    v.inBusy = usb_host_transfer_submit(v.in) == ESP_OK;
  }
  if (v.epOut && usb_host_transfer_alloc(TX_N, 0, &v.out) == ESP_OK) {
    v.out->device_handle = v.h;
    v.out->bEndpointAddress = v.epOut;
    v.out->callback = outDone;
    v.out->context = (void*)(uintptr_t)i;
  } else {
    v.epOut = 0;
  }
  midi::mount(i, dd->idVendor, dd->idProduct, name);
}

static void gone(usb_device_handle_t h) {
  for (int i = 0; i < MIDI_USB; i++) {
    Dev& v = dev[i];
    if (v.state == OPEN && v.h == h) {
      v.state = CLOSING;
      midi::umount(i);
      // what is under way ends (canceled), and then it can go
      for (uint8_t ep : {v.epIn, v.epOut}) {
        if (ep) {
          usb_host_endpoint_halt(h, ep);
          usb_host_endpoint_flush(h, ep);
        }
      }
    }
  }
}

static void clientEvent(const usb_host_client_event_msg_t* m, void*) {
  if (m->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    hal::log("usb: a new device, address %u", m->new_dev.address);
  }
  if (m->event == USB_HOST_CLIENT_EVENT_NEW_DEV && nNew < sizeof newAddr) {
    newAddr[nNew++] = m->new_dev.address;
  } else if (m->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    gone(m->dev_gone.dev_hdl);
  }
}

// the devices the library has: any new one is opened (as well as the NEW_DEV event, which doesn't always come)
static void poll() {
  static uint32_t at;
  uint32_t now = millis();
  if (now - at < 500) {
    return;
  }
  at = now;
  uint8_t list[16];
  int n = 0;
  if (usb_host_device_addr_list_fill(sizeof list, list, &n) != ESP_OK) {
    return;
  }
  bool here[128] = {};
  for (int i = 0; i < n; i++) {
    here[list[i] & 127] = true;
    if (!seen[list[i] & 127]) {
      hal::log("usb: the library has device %u", list[i]);
      openDev(list[i]);
    }
  }
  for (int a = 0; a < 128; a++) {
    if (seen[a] && !here[a]) {
      seen[a] = false;  // gone: if it comes back, it's looked at again
    }
  }
}

// ---------------------------------------------------------------------------------------------- run ---
static void libTask(void*) {
  for (;;) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      usb_host_device_free_all();
    }
  }
}

static void hostTask(void*) {
  usb_host_client_config_t cc = {};
  cc.is_synchronous = false;
  cc.max_num_event_msg = 8;
  cc.async.client_event_callback = clientEvent;
  ESP_ERROR_CHECK(usb_host_client_register(&cc, &client));
  for (;;) {
    usb_host_client_handle_events(client, 1);  // waits a tick at most
    for (uint8_t k = 0; k < nNew; k++) {
      openDev(newAddr[k]);
    }
    nNew = 0;
    uint32_t now = millis();
    for (int i = 0; i < MIDI_USB; i++) {
      Dev& v = dev[i];
      if (v.state == CLOSING && !v.inBusy && !v.outBusy) {
        closeDev(v);
      } else if (v.state == OPEN) {
        bool any = false;
        while (v.rr != v.rw) {
          midi::in(i, v.rx[v.rr % RX_N]);
          v.rr++;
          any = true;
        }
        if (any) {
          blink();
        }
        if (v.in && !v.inBusy && v.inRetryAt && (int32_t)(now - v.inRetryAt) >= 0) {
          v.inRetryAt = 0;
          usb_host_endpoint_clear(v.h, v.epIn);
          v.in->num_bytes = v.mpsIn;
          v.inBusy = usb_host_transfer_submit(v.in) == ESP_OK;
        }
      }
    }
    midi::service();
    ledLoop();
    poll();
  }
}

void board::usbHostBegin() {
  usb_host_config_t hc = {};
  hc.skip_phy_setup = false;
  hc.intr_flags = ESP_INTR_FLAG_LEVEL1;
  // the framework is built with ENUM_FILTER_CALLBACK, and without a filter every device is turned away (with only a
  // warning, which isn't even compiled in): take them all, in their first configuration
  hc.enum_filter_cb = [](const usb_device_desc_t*, uint8_t* config) {
    *config = 1;
    return true;
  };
  esp_err_t e = usb_host_install(&hc);
  if (e != ESP_OK) {
    hal::log("usb: can't start the USB host (%s)", esp_err_to_name(e));
    return;
  }
  hostOn = true;
  // ESP-IDF's own messages (errors: a device that doesn't enumerate, ...) into our log too
  esp_log_set_vprintf([](const char* fmt, va_list a) {
    char buf[160];
    int n = vsnprintf(buf, sizeof buf, fmt, a);
    while (n > 0 && n < (int)sizeof buf && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
      buf[--n] = 0;
    }
    hal::log("idf: %s", buf);
    return n;
  });
  xTaskCreatePinnedToCore(libTask, "usblib", 4096, 0, 6, 0, 1);
  xTaskCreatePinnedToCore(hostTask, "usbhost", 8192, 0, 5, 0, 1);
  hal::log("usb: host");
}
