// The box on the ESP32-S3 (s3box.h): USB MIDI on the ESP-IDF USB host, for box.cpp
#include "s3box.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <usb/usb_host.h>

#include "box.h"
#include "link.h"

#define RX_N 64   // packets kept from each device
#define TX_N 256  // bytes (64 packets) waiting to go to each device
static QueueHandle_t toBox, fromBox;
static usb_host_client_handle_t client;
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
  uint32_t rw, rr;
  uint8_t tx[TX_N];
  uint32_t txn;
  uint8_t st, nd, d[2];  // MIDI bytes into packets (usbStreamWrite), with running status
  bool sx;
  uint8_t sxb[3], sxn;
} dev[LINK_USB];

static uint8_t newAddr[8], nNew;  // devices that came, opened in the loop (not in the event callback)

// --------------------------------------------------------------------------------------- the light ---
void boxActivity() {
#ifdef LED_RGB
  if (!ledOffAt) {
    rgbLedWrite(LED_RGB, 24, 24, 24);
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

// -------------------------------------------------------------------------------------- the queues ---
void boxOut(uint8_t type, uint8_t d, const uint8_t* p, uint32_t n) {
  static HostFrame f;
  f.type = type;
  f.dev = d;
  f.n = (uint16_t)(n > LINK_PAYLOAD ? LINK_PAYLOAD : n);
  memcpy(f.p, p, f.n);
  // MIDI that doesn't fit is dropped, rather than holding up the USB; the rest waits a little
  xQueueSend(fromBox, &f, type == LINK_MIDI || type == LINK_SEND ? 0 : pdMS_TO_TICKS(200));
}

bool boxIn(HostFrame& f) {
  return xQueueReceive(toBox, &f, 0) == pdTRUE;
}

void s3box::send(uint8_t type, uint8_t d, const uint8_t* p, uint32_t n) {
  static HostFrame f;
  f.type = type;
  f.dev = d;
  f.n = (uint16_t)(n > LINK_PAYLOAD ? LINK_PAYLOAD : n);
  memcpy(f.p, p, f.n);
  xQueueSend(toBox, &f, portMAX_DELAY);
}

void s3box::pump() {
  static HostFrame f;
  while (xQueueReceive(fromBox, &f, 0) == pdTRUE) {
    hub.frame(f.type, f.dev, f.p, f.n);
  }
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

bool usbMounted(uint8_t i) {
  return i < LINK_USB && dev[i].state == OPEN;
}

bool usbRead(uint8_t i, uint8_t p[4]) {
  Dev& v = dev[i];
  if (v.rr == v.rw) {
    return false;
  }
  memcpy(p, v.rx[v.rr % RX_N], 4);
  v.rr++;
  return true;
}

void usbWrite(uint8_t i, const uint8_t p[4]) {
  Dev& v = dev[i];
  if (v.epOut && v.txn + 4 <= TX_N) {
    memcpy(v.tx + v.txn, p, 4);
    v.txn += 4;
  }
}

void usbFlush(uint8_t i) {
  Dev& v = dev[i];
  if (v.state != OPEN || v.outBusy || !v.txn) {
    return;
  }
  memcpy(v.out->data_buffer, v.tx, v.txn);
  v.out->num_bytes = v.txn;
  v.txn = 0;
  v.outBusy = usb_host_transfer_submit(v.out) == ESP_OK;
}

static void packet(Dev& v, uint8_t cin, uint8_t a, uint8_t b, uint8_t c) {
  uint8_t p[4] = {cin, a, b, c};
  if (v.txn + 4 <= TX_N) {
    memcpy(v.tx + v.txn, p, 4);
    v.txn += 4;
  }
}

// one MIDI byte into packets (as tuh_midi_stream_write does): false when there is no room for it
static bool streamByte(Dev& v, uint8_t b) {
  if (v.txn + 4 > TX_N) {
    return false;
  }
  if (b >= 0xF8) {  // realtime: anywhere, even inside a SysEx
    packet(v, 0xF, b, 0, 0);
  } else if (b == 0xF0) {
    v.sx = true;
    v.sxb[0] = b;
    v.sxn = 1;
    v.st = 0;
  } else if (b == 0xF7) {
    if (v.sx) {
      v.sxb[v.sxn] = b;
      packet(v, (uint8_t)(5 + v.sxn), v.sxb[0], v.sxn > 0 ? v.sxb[1] : 0, v.sxn == 2 ? v.sxb[2] : 0);
      v.sx = false;
    }
  } else if (b & 0x80) {
    v.sx = false;
    v.nd = 0;
    v.st = b;
    if (b == 0xF6) {  // tune request: no data
      packet(v, 0x5, b, 0, 0);
      v.st = 0;
    } else if (b >= 0xF4) {  // undefined
      v.st = 0;
    }
  } else if (v.sx) {
    v.sxb[v.sxn++] = b;
    if (v.sxn == 3) {
      packet(v, 0x4, v.sxb[0], v.sxb[1], v.sxb[2]);
      v.sxn = 0;
    }
  } else if (v.st) {
    uint8_t hi = v.st >> 4;
    uint8_t need = v.st < 0xF0 ? (hi == 0xC || hi == 0xD ? 1 : 2) : v.st == 0xF2 ? 2
                                                                                 : 1;
    v.d[v.nd++] = b;
    if (v.nd == need) {
      uint8_t cin = v.st < 0xF0 ? hi : need == 1 ? 0x2
                                                 : 0x3;
      packet(v, cin, v.st, v.d[0], need > 1 ? v.d[1] : 0);
      v.nd = 0;
      if (v.st >= 0xF0) {
        v.st = 0;  // system common: no running status
      }
    }
  }
  return true;
}

uint32_t usbStreamWrite(uint8_t i, const uint8_t* p, uint32_t n) {
  Dev& v = dev[i];
  if (!v.epOut) {
    return n;  // nowhere to go
  }
  uint32_t k = 0;
  while (k < n && streamByte(v, p[k])) {
    k++;
  }
  return k;
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
  while (i < LINK_USB && dev[i].state != FREE) {
    i++;
  }
  if (i == LINK_USB) {
    Serial.printf("usb: device %u: no room (%d devices)\n", addr, LINK_USB);
    return;
  }
  Dev& v = dev[i];
  memset(&v, 0, sizeof v);
  if (usb_host_device_open(client, addr, &v.h) != ESP_OK) {
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
  if (dd->bDeviceClass == USB_CLASS_HUB || !findMidi(cfg, v, alt)) {
    usb_host_device_close(client, v.h);  // not MIDI (a hub is the hub driver's)
    return;
  }
  esp_err_t e = usb_host_interface_claim(client, v.h, v.iface, alt);
  if (e != ESP_OK) {
    // usually: the S3's USB channels are all taken
    Serial.printf("usb: %04X:%04X: can't claim its MIDI interface (%s)\n", dd->idVendor, dd->idProduct,
                  esp_err_to_name(e));
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
  Serial.printf("usb: %04X:%04X is device %d\n", dd->idVendor, dd->idProduct, i);
  box::mount(i, dd->idVendor, dd->idProduct, (const uint8_t*)info.str_desc_product);
}

static void gone(usb_device_handle_t h) {
  for (int i = 0; i < LINK_USB; i++) {
    Dev& v = dev[i];
    if (v.state == OPEN && v.h == h) {
      v.state = CLOSING;
      box::umount(i);
      // what is under way ends (canceled), and then it can go
      for (uint8_t ep : {v.epIn, v.epOut}) {
        if (ep) {
          usb_host_endpoint_halt(h, ep);
          usb_host_endpoint_flush(h, ep);
        }
      }
      Serial.printf("usb: device %d went\n", i);
    }
  }
}

static void clientEvent(const usb_host_client_event_msg_t* m, void*) {
  if (m->event == USB_HOST_CLIENT_EVENT_NEW_DEV && nNew < sizeof newAddr) {
    newAddr[nNew++] = m->new_dev.address;
  } else if (m->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    gone(m->dev_gone.dev_hdl);
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

static void boxTask(void*) {
  usb_host_client_config_t cc = {};
  cc.is_synchronous = false;
  cc.max_num_event_msg = 8;
  cc.async.client_event_callback = clientEvent;
  ESP_ERROR_CHECK(usb_host_client_register(&cc, &client));
  box::begin();
  for (;;) {
    usb_host_client_handle_events(client, 1);  // waits a tick at most
    for (uint8_t k = 0; k < nNew; k++) {
      openDev(newAddr[k]);
    }
    nNew = 0;
    uint32_t now = millis();
    for (Dev& v : dev) {
      if (v.state == CLOSING && !v.inBusy && !v.outBusy) {
        closeDev(v);
      } else if (v.state == OPEN && v.in && !v.inBusy && v.inRetryAt && (int32_t)(now - v.inRetryAt) >= 0) {
        v.inRetryAt = 0;
        usb_host_endpoint_clear(v.h, v.epIn);
        v.in->num_bytes = v.mpsIn;
        v.inBusy = usb_host_transfer_submit(v.in) == ESP_OK;
      }
    }
    box::loop();
    ledLoop();
  }
}

void s3box::begin() {
  toBox = xQueueCreate(32, sizeof(HostFrame));
  fromBox = xQueueCreate(64, sizeof(HostFrame));
#ifdef LED_RGB
  rgbLedWrite(LED_RGB, 24, 24, 24);  // it's alive
  ledOffAt = millis() + 300;
#endif
  usb_host_config_t hc = {};
  hc.skip_phy_setup = false;
  hc.intr_flags = ESP_INTR_FLAG_LEVEL1;
  esp_err_t e = usb_host_install(&hc);
  if (e != ESP_OK) {
    Serial.printf("usb: can't start the USB host (%s)\n", esp_err_to_name(e));
    return;
  }
  xTaskCreatePinnedToCore(libTask, "usblib", 4096, 0, 6, 0, 1);
  xTaskCreatePinnedToCore(boxTask, "box", 8192, 0, 5, 0, 1);
}
