// The box, on core 1 (host.h): on the Feather's USB-A (PIO USB), or the board's own USB port (the ultra-light
// build). Started as tools/usbhost in FOMAJAM. Besides the devices on the hub, a CYD may have its own (Bluetooth
// MIDI, fm1link.h): they are routed and mapped the same way, their MIDI coming and going over the link.
#include "host.h"

#include <Adafruit_TinyUSB.h>

#include "fm1map.h"
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

void hostQueues() {
  queue_init(&toHost, sizeof(HostFrame), 16);
  queue_init(&fromHost, sizeof(HostFrame), 48);
}

/* to core 0: MIDI that doesn't fit is dropped, rather than holding up the USB */
static void link_send(uint8_t type, uint8_t dev, const uint8_t* p, uint32_t n) {
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

/* ---------------------------------------------------------------------------------------- the devices --- */
static struct {
  uint8_t daddr;
  uint8_t named; /* 0: product string not asked yet, 1: asked, 2: have it (MOUNT sent) */
  uint16_t vid, pid;
  uint8_t desc[64]; /* the string descriptor */
  uint8_t out[LINK_PAYLOAD];
  uint32_t out_n, out_at; /* LINK_SEND bytes not written yet */
} dev[CFG_TUH_MIDI];
static_assert(CFG_TUH_MIDI <= LINK_USB, "more USB MIDI devices than the link has room for");

/* the CYD's own devices (LINK_USB and up): what they send, read into USB MIDI packets */
#define REMOTE (LINK_DEVS - LINK_USB)
static struct {
  bool on;
  uint8_t st, nd, d[2]; /* a message, as it comes in (with running status) */
  bool sx;
  uint8_t sxb[3], sxn; /* SysEx, 3 bytes to a packet */
} rem[REMOTE];

static uint8_t route[LINK_DEVS];
static map_rule_t rules[MAP_RULES];
static uint32_t n_rules;

static void send_mount(uint8_t idx) {
  uint8_t p[4 + 31];
  uint32_t n = 4;
  p[0] = (uint8_t)dev[idx].vid;
  p[1] = (uint8_t)(dev[idx].vid >> 8);
  p[2] = (uint8_t)dev[idx].pid;
  p[3] = (uint8_t)(dev[idx].pid >> 8);
  if (dev[idx].named == 2) {
    /* UTF-16LE after the 2-byte header: keep printable ASCII */
    uint32_t len = dev[idx].desc[0] <= sizeof dev[idx].desc ? dev[idx].desc[0] : sizeof dev[idx].desc;
    for (uint32_t i = 2; i + 1 < len && n < sizeof p; i += 2) {
      uint8_t c = dev[idx].desc[i];
      if (dev[idx].desc[i + 1] == 0 && c >= 0x20 && c < 0x7F) {
        p[n++] = c;
      }
    }
  }
  link_send(LINK_MOUNT, idx, p, n);
}

static void name_done(tuh_xfer_t* xfer) {
  uint8_t idx = (uint8_t)xfer->user_data;
  if (idx < CFG_TUH_MIDI && tuh_midi_mounted(idx) && dev[idx].daddr == xfer->daddr) {
    if (xfer->result != XFER_RESULT_SUCCESS) {
      dev[idx].desc[0] = 0;
    }
    dev[idx].named = 2;
    send_mount(idx);
  }
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t* info) {
  if (idx >= CFG_TUH_MIDI) {
    return;
  }
  dev[idx].daddr = info->daddr;
  dev[idx].named = 0;
  dev[idx].out_n = dev[idx].out_at = 0;
  tuh_vid_pid_get(info->daddr, &dev[idx].vid, &dev[idx].pid);
}

void tuh_midi_umount_cb(uint8_t idx) {
  if (idx < CFG_TUH_MIDI) {
    dev[idx].named = 0;
    dev[idx].out_n = dev[idx].out_at = 0;
    link_send(LINK_UMOUNT, idx, 0, 0);
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

/* --------------------------------------------------------------------------------------- the traffic --- */
static const uint8_t CIN_LEN[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};

static bool mounted(uint8_t d) {
  return d < LINK_USB ? d < CFG_TUH_MIDI && tuh_midi_mounted(d) : d < LINK_DEVS && rem[d - LINK_USB].on;
}

/* MIDI bytes for the link, gathered and sent as one frame: what each device sent (LINK_MIDI, for whoever
 * listens), and what goes to each of the CYD's devices (LINK_SEND) */
static struct {
  uint8_t b[LINK_PAYLOAD];
  uint32_t n;
} heard[LINK_DEVS], to_cyd[REMOTE];

static void gather(uint8_t type, uint8_t d, const uint8_t* p, uint32_t n) {
  uint8_t* b = type == LINK_MIDI ? heard[d].b : to_cyd[d - LINK_USB].b;
  uint32_t* bn = type == LINK_MIDI ? &heard[d].n : &to_cyd[d - LINK_USB].n;
  if (*bn + n > LINK_PAYLOAD) {
    link_send(type, d, b, *bn);
    *bn = 0;
  }
  memcpy(b + *bn, p, n);
  *bn += n;
}

static void gather_flush(void) {
  for (uint8_t d = 0; d < LINK_DEVS; d++) {
    if (heard[d].n) {
      link_send(LINK_MIDI, d, heard[d].b, heard[d].n);
      heard[d].n = 0;
    }
    if (d >= LINK_USB && to_cyd[d - LINK_USB].n) {
      link_send(LINK_SEND, d, to_cyd[d - LINK_USB].b, to_cyd[d - LINK_USB].n);
      to_cyd[d - LINK_USB].n = 0;
    }
  }
}

/* one USB MIDI packet (cable 0) from device `from`: to whoever listens, and through the mapping to the others */
static void pass(uint8_t from, const uint8_t* p) {
  gather(LINK_MIDI, from, p + 1, CIN_LEN[p[0]]); /* the CYD sees it as it came */
  if (!led_off_at) {
    led(true);
  }
  led_off_at = millis() + 20;
  uint8_t q[4] = {p[0], p[1], p[2], p[3]}, dest = route[from];
  if (p[0] >= 0x8 && p[0] <= 0xE && n_rules) {
    uint8_t o[3];
    uint32_t on;
    int k = map_apply(rules, n_rules, from, p + 1, o, &on);
    if (k >= 0 && !on) {
      return; /* dropped */
    }
    if (k >= 0) {
      q[0] = o[0] >> 4;
      q[1] = o[0];
      q[2] = o[1];
      q[3] = on > 2 ? o[2] : 0;
      if (rules[k].to) {
        dest = rules[k].to;
      }
    }
  }
  for (uint8_t to = 0; to < LINK_DEVS; to++) {
    if (to != from && (dest >> to & 1) && mounted(to)) {
      if (to < LINK_USB) {
        tuh_midi_packet_write(to, q);
      } else {
        gather(LINK_SEND, to, q + 1, CIN_LEN[q[0]]);
      }
    }
  }
}

static void forward(void) {
  uint8_t p[4];
  for (uint8_t from = 0; from < CFG_TUH_MIDI; from++) {
    if (!tuh_midi_mounted(from)) {
      continue;
    }
    while (tuh_midi_packet_read(from, p)) {
      p[0] &= 0x0F; /* every message to each device's first port (cable 0) */
      pass(from, p);
    }
  }
}

/* a byte one of the CYD's devices sent: into USB MIDI packets */
static void remote_byte(uint8_t from, uint8_t b) {
  auto& r = rem[from - LINK_USB];
  if (b >= 0xF8) { /* realtime: anywhere, even inside a SysEx */
    uint8_t p[4] = {0xF, b, 0, 0};
    pass(from, p);
  } else if (b == 0xF0) {
    r.sx = true;
    r.sxb[0] = b;
    r.sxn = 1;
    r.st = 0;
  } else if (b == 0xF7) {
    if (r.sx) {
      r.sxb[r.sxn] = b;
      uint8_t p[4] = {(uint8_t)(5 + r.sxn), r.sxb[0], r.sxn > 0 ? r.sxb[1] : (uint8_t)0, 0};
      if (r.sxn == 2) {
        p[3] = r.sxb[2];
      }
      pass(from, p);
      r.sx = false;
    }
  } else if (b & 0x80) {
    r.sx = false; /* an unfinished SysEx: what's sent is sent */
    r.nd = 0;
    r.st = b;
    if (b == 0xF6) { /* tune request: no data */
      uint8_t p[4] = {0x5, b, 0, 0};
      pass(from, p);
      r.st = 0;
    } else if (b >= 0xF4) { /* undefined */
      r.st = 0;
    }
  } else if (r.sx) {
    r.sxb[r.sxn++] = b;
    if (r.sxn == 3) {
      uint8_t p[4] = {0x4, r.sxb[0], r.sxb[1], r.sxb[2]};
      pass(from, p);
      r.sxn = 0;
    }
  } else if (r.st) {
    uint8_t need = r.st < 0xF0 ? (uint8_t)(map_len(r.st) - 1) : r.st == 0xF2 ? 2
                                                                             : 1;
    r.d[r.nd++] = b;
    if (r.nd == need) {
      uint8_t cin = r.st < 0xF0 ? r.st >> 4 : need == 1 ? 0x2
                                                        : 0x3;
      uint8_t p[4] = {cin, r.st, r.d[0], need > 1 ? r.d[1] : (uint8_t)0};
      pass(from, p);
      r.nd = 0;
      if (r.st >= 0xF0) {
        r.st = 0; /* system common: no running status */
      }
    }
  }
}

/* what core 0 (or the CYD through it) sent: one LINK_SEND at a time, written as the device's TX buffer has room */
static void from_link(void) {
  for (uint8_t i = 0; i < CFG_TUH_MIDI; i++) {
    if (dev[i].out_at < dev[i].out_n) {
      if (!tuh_midi_mounted(i)) {
        dev[i].out_n = dev[i].out_at = 0;
        continue;
      }
      dev[i].out_at += tuh_midi_stream_write(i, 0, dev[i].out + dev[i].out_at, dev[i].out_n - dev[i].out_at);
      return; /* the rest waits: keeps the order of what the CYD sent */
    }
  }
  static HostFrame rx;
  while (queue_try_remove(&toHost, &rx)) {
    if (rx.dev >= LINK_USB && rx.dev < LINK_DEVS &&
        (rx.type == LINK_MOUNT || rx.type == LINK_UMOUNT || rx.type == LINK_MIDI)) {
      /* one of the CYD's devices: everyone hears of it, as of one on the hub */
      auto& r = rem[rx.dev - LINK_USB];
      if (rx.type == LINK_MIDI) {
        for (uint32_t i = 0; r.on && i < rx.n; i++) {
          remote_byte(rx.dev, rx.p[i]);
        }
      } else {
        memset(&r, 0, sizeof r);
        r.on = rx.type == LINK_MOUNT;
        to_cyd[rx.dev - LINK_USB].n = 0;
        link_send(rx.type, rx.dev, rx.p, rx.n);
      }
    } else if (rx.type == LINK_HELLO) {
      /* the CYD (re)started: its devices are gone (it mounts them again after our HELLO) */
      memset(rem, 0, sizeof rem);
      uint8_t v = LINK_VERSION;
      link_send(LINK_HELLO, 0, &v, 1);
      for (uint8_t i = 0; i < CFG_TUH_MIDI; i++) {
        if (tuh_midi_mounted(i) && dev[i].named == 2) {
          send_mount(i);
        }
      }
    } else if (rx.type == LINK_ROUTE) {
      for (uint8_t i = 0; i < LINK_DEVS && i < rx.n; i++) {
        route[i] = rx.p[i];
      }
    } else if (rx.type == LINK_MAP) {
      n_rules = rx.n / sizeof(map_rule_t);
      if (n_rules > MAP_RULES) {
        n_rules = MAP_RULES;
      }
      for (uint32_t i = 0; i < n_rules; i++) {
        const uint8_t* b = rx.p + i * 8;
        map_rule_t r = {b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]};
        rules[i] = r;
      }
    } else if (rx.type == LINK_SEND && rx.dev < CFG_TUH_MIDI && tuh_midi_mounted(rx.dev) && rx.n) {
      memcpy(dev[rx.dev].out, rx.p, rx.n);
      dev[rx.dev].out_n = rx.n;
      dev[rx.dev].out_at = tuh_midi_stream_write(rx.dev, 0, dev[rx.dev].out, rx.n);
      return;
    }
  }
}

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
  for (uint8_t i = 0; i < LINK_DEVS; i++) {
    route[i] = LINK_ROUTE_ALL;
  }
  uint8_t v = LINK_VERSION;
  link_send(LINK_HELLO, 0, &v, 1);
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
  forward();
  from_link();
  gather_flush();
  for (uint8_t to = 0; to < CFG_TUH_MIDI; to++) {
    if (tuh_midi_mounted(to)) {
      tuh_midi_write_flush(to);
    }
  }
  if (led_off_at && (int32_t)(millis() - led_off_at) >= 0) {
    led(false);
    led_off_at = 0;
  }
}
