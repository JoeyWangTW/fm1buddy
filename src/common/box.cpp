// The box (box.h). Started as tools/usbhost in FOMAJAM. Besides the devices on the hub, the screen may have its
// own (Bluetooth MIDI, fm1link.h): they are routed and mapped the same way, their MIDI coming and going over the
// link.
#include "box.h"

#include "fm1map.h"

/* ---------------------------------------------------------------------------------------- the devices --- */
static struct {
  bool named; /* MOUNT sent */
  uint16_t vid, pid;
  uint8_t desc[64]; /* the product string descriptor */
  uint8_t out[LINK_PAYLOAD];
  uint32_t out_n, out_at; /* LINK_SEND bytes not written yet */
} dev[LINK_USB];

/* the screen's own devices (LINK_USB and up): what they send, read into USB MIDI packets */
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
  /* UTF-16LE after the 2-byte header: keep printable ASCII */
  uint32_t len = dev[idx].desc[0] <= sizeof dev[idx].desc ? dev[idx].desc[0] : sizeof dev[idx].desc;
  for (uint32_t i = 2; i + 1 < len && n < sizeof p; i += 2) {
    uint8_t c = dev[idx].desc[i];
    if (dev[idx].desc[i + 1] == 0 && c >= 0x20 && c < 0x7F) {
      p[n++] = c;
    }
  }
  boxOut(LINK_MOUNT, idx, p, n);
}

void box::mount(uint8_t i, uint16_t vid, uint16_t pid, const uint8_t* desc) {
  if (i >= LINK_USB) {
    return;
  }
  dev[i].vid = vid;
  dev[i].pid = pid;
  dev[i].desc[0] = 0;
  if (desc && desc[0] >= 2) {
    memcpy(dev[i].desc, desc, desc[0] < sizeof dev[i].desc ? desc[0] : sizeof dev[i].desc);
  }
  dev[i].out_n = dev[i].out_at = 0;
  dev[i].named = true;
  send_mount(i);
}

void box::umount(uint8_t i) {
  if (i < LINK_USB) {
    dev[i].named = false;
    dev[i].out_n = dev[i].out_at = 0;
    boxOut(LINK_UMOUNT, i, 0, 0);
  }
}

/* --------------------------------------------------------------------------------------- the traffic --- */
static const uint8_t CIN_LEN[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};

static bool mounted(uint8_t d) {
  return d < LINK_USB ? usbMounted(d) : d < LINK_DEVS && rem[d - LINK_USB].on;
}

/* MIDI bytes for the link, gathered and sent as one frame: what each device sent (LINK_MIDI, for whoever
 * listens), and what goes to each of the screen's devices (LINK_SEND) */
static struct {
  uint8_t b[LINK_PAYLOAD];
  uint32_t n;
} heard[LINK_DEVS], to_cyd[REMOTE];

static void gather(uint8_t type, uint8_t d, const uint8_t* p, uint32_t n) {
  uint8_t* b = type == LINK_MIDI ? heard[d].b : to_cyd[d - LINK_USB].b;
  uint32_t* bn = type == LINK_MIDI ? &heard[d].n : &to_cyd[d - LINK_USB].n;
  if (*bn + n > LINK_PAYLOAD) {
    boxOut(type, d, b, *bn);
    *bn = 0;
  }
  memcpy(b + *bn, p, n);
  *bn += n;
}

static void gather_flush(void) {
  for (uint8_t d = 0; d < LINK_DEVS; d++) {
    if (heard[d].n) {
      boxOut(LINK_MIDI, d, heard[d].b, heard[d].n);
      heard[d].n = 0;
    }
    if (d >= LINK_USB && to_cyd[d - LINK_USB].n) {
      boxOut(LINK_SEND, d, to_cyd[d - LINK_USB].b, to_cyd[d - LINK_USB].n);
      to_cyd[d - LINK_USB].n = 0;
    }
  }
}

/* one USB MIDI packet (cable 0) from device `from`: to whoever listens, and through the mapping to the others */
static void pass(uint8_t from, const uint8_t* p) {
  gather(LINK_MIDI, from, p + 1, CIN_LEN[p[0]]); /* the screen sees it as it came */
  boxActivity();
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
        usbWrite(to, q);
      } else {
        gather(LINK_SEND, to, q + 1, CIN_LEN[q[0]]);
      }
    }
  }
}

static void forward(void) {
  uint8_t p[4];
  for (uint8_t from = 0; from < LINK_USB; from++) {
    if (!usbMounted(from)) {
      continue;
    }
    while (usbRead(from, p)) {
      p[0] &= 0x0F; /* every message to each device's first port (cable 0) */
      pass(from, p);
    }
  }
}

/* a byte one of the screen's devices sent: into USB MIDI packets */
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

/* what the screen sent: one LINK_SEND at a time, written as the device's TX buffer has room */
static void from_link(void) {
  for (uint8_t i = 0; i < LINK_USB; i++) {
    if (dev[i].out_at < dev[i].out_n) {
      if (!usbMounted(i)) {
        dev[i].out_n = dev[i].out_at = 0;
        continue;
      }
      dev[i].out_at += usbStreamWrite(i, dev[i].out + dev[i].out_at, dev[i].out_n - dev[i].out_at);
      return; /* the rest waits: keeps the order of what the screen sent */
    }
  }
  static HostFrame rx;
  while (boxIn(rx)) {
    if (rx.dev >= LINK_USB && rx.dev < LINK_DEVS &&
        (rx.type == LINK_MOUNT || rx.type == LINK_UMOUNT || rx.type == LINK_MIDI)) {
      /* one of the screen's devices: everyone hears of it, as of one on the hub */
      auto& r = rem[rx.dev - LINK_USB];
      if (rx.type == LINK_MIDI) {
        for (uint32_t i = 0; r.on && i < rx.n; i++) {
          remote_byte(rx.dev, rx.p[i]);
        }
      } else {
        memset(&r, 0, sizeof r);
        r.on = rx.type == LINK_MOUNT;
        to_cyd[rx.dev - LINK_USB].n = 0;
        boxOut(rx.type, rx.dev, rx.p, rx.n);
      }
    } else if (rx.type == LINK_HELLO) {
      /* the screen (re)started: its devices are gone (it mounts them again after our HELLO) */
      memset(rem, 0, sizeof rem);
      uint8_t v = LINK_VERSION;
      boxOut(LINK_HELLO, 0, &v, 1);
      for (uint8_t i = 0; i < LINK_USB; i++) {
        if (usbMounted(i) && dev[i].named) {
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
    } else if (rx.type == LINK_SEND && rx.dev < LINK_USB && usbMounted(rx.dev) && rx.n) {
      memcpy(dev[rx.dev].out, rx.p, rx.n);
      dev[rx.dev].out_n = rx.n;
      dev[rx.dev].out_at = usbStreamWrite(rx.dev, dev[rx.dev].out, rx.n);
      return;
    }
  }
}

void box::begin() {
  for (uint8_t i = 0; i < LINK_DEVS; i++) {
    route[i] = LINK_ROUTE_ALL;
  }
  uint8_t v = LINK_VERSION;
  boxOut(LINK_HELLO, 0, &v, 1);
}

void box::loop() {
  forward();
  from_link();
  gather_flush();
  for (uint8_t to = 0; to < LINK_USB; to++) {
    if (usbMounted(to)) {
      usbFlush(to);
    }
  }
}
