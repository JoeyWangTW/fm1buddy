// The box on the RP2040, on core 1 (host.h): the USB host for box.cpp, on the Feather's USB-A (PIO USB) or the
// board's own USB port (the ultra-light build), and the queues to core 0.
#include "host.h"

#include <Adafruit_TinyUSB.h>

#include "box.h"
#ifdef USE_TINYUSB_HOST
#include "stick.h"
#else
#include "pio_usb.h"
#endif
#ifdef LED_RGB
#include <Adafruit_NeoPixel.h>
#endif

queue_t toHost, fromHost;
static Adafruit_USBH_Host USBHost;
static uint32_t led_off_at;

#ifdef LED_RGB
static Adafruit_NeoPixel rgb(1, LED_RGB, NEO_GRB + NEO_KHZ800);
#endif

/* the activity light: the Feather's red LED, or an RGB LED (dim white) */
static void led(bool on) {
#ifdef LED_RGB
  rgb.setPixelColor(0, on ? rgb.Color(24, 24, 24) : 0);
  rgb.show();
#else
  digitalWrite(PIN_LED, on);
#endif
}

void boxActivity() {
  if (!led_off_at) {
    led(true);
  }
  led_off_at = millis() + 20;
}

void hostQueues() {
  queue_init(&toHost, sizeof(HostFrame), 16);
  queue_init(&fromHost, sizeof(HostFrame), 48);
}

/* to core 0: MIDI that doesn't fit is dropped, rather than holding up the USB */
void boxOut(uint8_t type, uint8_t dev, const uint8_t* p, uint32_t n) {
  static HostFrame f;
  f.type = type;
  f.dev = dev;
  f.n = (uint16_t)(n > LINK_PAYLOAD ? LINK_PAYLOAD : n);
  memcpy(f.p, p, f.n);
  if (type == LINK_MIDI || type == LINK_SEND) {
    queue_try_add(&fromHost, &f);
  } else {
    queue_add_blocking(&fromHost, &f);
  }
}

bool boxIn(HostFrame& f) {
  return queue_try_remove(&toHost, &f);
}

/* ---------------------------------------------------------------------------------------- the devices --- */
static struct {
  uint8_t daddr;
  uint8_t named; /* 0: product string not asked yet, 1: asked, 2: have it (box::mount called) */
  uint16_t vid, pid;
  uint8_t desc[64]; /* the string descriptor */
} dev[CFG_TUH_MIDI];
static_assert(CFG_TUH_MIDI <= LINK_USB, "more USB MIDI devices than the link has room for");

static void name_done(tuh_xfer_t* xfer) {
  uint8_t idx = (uint8_t)xfer->user_data;
  if (idx < CFG_TUH_MIDI && tuh_midi_mounted(idx) && dev[idx].daddr == xfer->daddr) {
    dev[idx].named = 2;
    box::mount(idx, dev[idx].vid, dev[idx].pid, xfer->result == XFER_RESULT_SUCCESS ? dev[idx].desc : 0);
  }
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t* info) {
  if (idx >= CFG_TUH_MIDI) {
    return;
  }
  dev[idx].daddr = info->daddr;
  dev[idx].named = 0;
  tuh_vid_pid_get(info->daddr, &dev[idx].vid, &dev[idx].pid);
}

void tuh_midi_umount_cb(uint8_t idx) {
  if (idx < CFG_TUH_MIDI) {
    dev[idx].named = 0;
    box::umount(idx);
  }
}

/* the product string: asked once the control endpoint is free (it is busy during the mount) */
static void ask_names(void) {
  for (uint8_t i = 0; i < CFG_TUH_MIDI; i++) {
    if (tuh_midi_mounted(i) && dev[i].named == 0 &&
        tuh_descriptor_get_product_string(dev[i].daddr, 0x0409, dev[i].desc, sizeof dev[i].desc, name_done, i)) {
      dev[i].named = 1;
    }
  }
}

bool usbMounted(uint8_t i) {
  return i < CFG_TUH_MIDI && tuh_midi_mounted(i);
}

bool usbRead(uint8_t i, uint8_t p[4]) {
  return tuh_midi_packet_read(i, p);
}

void usbWrite(uint8_t i, const uint8_t p[4]) {
  tuh_midi_packet_write(i, p);
}

uint32_t usbStreamWrite(uint8_t i, const uint8_t* p, uint32_t n) {
  return tuh_midi_stream_write(i, 0, p, n);
}

void usbFlush(uint8_t i) {
  tuh_midi_write_flush(i);
}

/* ------------------------------------------------------------------------------------------------ run --- */
void hostSetup() {
#ifdef LED_RGB
  rgb.begin();
#else
  pinMode(PIN_LED, OUTPUT);
#endif
#ifdef PIN_5V_EN
  pinMode(PIN_5V_EN, OUTPUT);
  digitalWrite(PIN_5V_EN, HIGH);  // the USB-A socket's 5 V
#endif
  box::begin();
#ifdef USE_TINYUSB_HOST
  USBHost.begin(0);  // the board's own USB port
  led(true);         // it's alive
  led_off_at = millis() + 300;
#else
  pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
  cfg.pin_dp = PIN_USB_HOST_DP;
  USBHost.configure_pio_usb(1, &cfg);
  USBHost.begin(1);
#endif
}

void hostLoop() {
  USBHost.task();
  ask_names();
#ifdef USE_TINYUSB_HOST
  stickTask();
#endif
  box::loop();
  if (led_off_at && (int32_t)(millis() - led_off_at) >= 0) {
    led(false);
    led_off_at = 0;
  }
}
