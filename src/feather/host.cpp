// The box, on core 1 (host.h). Started as tools/usbhost in FOMAJAM.
#include "host.h"

#include <Adafruit_TinyUSB.h>

#include "fm1map.h"
#include "pio_usb.h"

queue_t toHost, fromHost;
static Adafruit_USBH_Host USBHost;
static uint32_t led_off_at;

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
  if (type == LINK_MIDI) {
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
static uint8_t route[CFG_TUH_MIDI];
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

static void forward(void) {
  uint8_t p[4];
  for (uint8_t from = 0; from < CFG_TUH_MIDI; from++) {
    if (!tuh_midi_mounted(from)) {
      continue;
    }
    uint8_t bytes[LINK_PAYLOAD];
    uint32_t nb = 0;
    while (tuh_midi_packet_read(from, p)) {
      p[0] &= 0x0F; /* every message to each device's first port (cable 0) */
      for (uint32_t k = 0; k < CIN_LEN[p[0]]; k++) {
        bytes[nb++] = p[1 + k]; /* the CYD sees it as it came */
      }
      if (nb > LINK_PAYLOAD - 3) {
        link_send(LINK_MIDI, from, bytes, nb);
        nb = 0;
      }
      digitalWrite(PIN_LED, 1);
      led_off_at = millis() + 20;
      uint8_t q[4] = {p[0], p[1], p[2], p[3]}, dest = route[from];
      if (p[0] >= 0x8 && p[0] <= 0xE && n_rules) {
        uint8_t o[3];
        uint32_t on;
        int k = map_apply(rules, n_rules, from, p + 1, o, &on);
        if (k >= 0 && !on) {
          continue; /* dropped */
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
      for (uint8_t to = 0; to < CFG_TUH_MIDI; to++) {
        if (to != from && (dest >> to & 1) && tuh_midi_mounted(to)) {
          tuh_midi_packet_write(to, q);
        }
      }
    }
    if (nb) {
      link_send(LINK_MIDI, from, bytes, nb);
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
    if (rx.type == LINK_HELLO) {
      uint8_t v = LINK_VERSION;
      link_send(LINK_HELLO, 0, &v, 1);
      for (uint8_t i = 0; i < CFG_TUH_MIDI; i++) {
        if (tuh_midi_mounted(i) && dev[i].named == 2) {
          send_mount(i);
        }
      }
    } else if (rx.type == LINK_ROUTE && rx.n >= CFG_TUH_MIDI) {
      for (uint8_t i = 0; i < CFG_TUH_MIDI; i++) {
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
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_5V_EN, OUTPUT);
  digitalWrite(PIN_5V_EN, HIGH);  // the USB-A socket's 5 V
  for (uint8_t i = 0; i < CFG_TUH_MIDI; i++) {
    route[i] = LINK_ROUTE_ALL;
  }
  uint8_t v = LINK_VERSION;
  link_send(LINK_HELLO, 0, &v, 1);
  pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
  cfg.pin_dp = PIN_USB_HOST_DP;
  USBHost.configure_pio_usb(1, &cfg);
  USBHost.begin(1);
}

void hostLoop() {
  USBHost.task();
  ask_names();
  forward();
  from_link();
  for (uint8_t to = 0; to < CFG_TUH_MIDI; to++) {
    if (tuh_midi_mounted(to)) {
      tuh_midi_write_flush(to);
    }
  }
  if (led_off_at && (int32_t)(millis() - led_off_at) >= 0) {
    digitalWrite(PIN_LED, 0);
    led_off_at = 0;
  }
}
