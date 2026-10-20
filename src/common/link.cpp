#include "link.h"

Link hub;

void Link::begin(LinkSend send, LinkPump pump) {
  memset(this, 0, sizeof *this);
  sendFn = send;
  pumpFn = pump;
}

void Link::out(uint8_t type, uint8_t d, const uint8_t* p, uint32_t n) {
  sendFn(type, d, p, n);
}

void Link::poll() {
  // ask until the box answers (it may boot after us); a HELLO from it also re-sends every device
  if (!box && millis() - helloAt > 1000) {
    helloAt = millis();
    out(LINK_HELLO, 0, 0, 0);
  }
  pumpFn();
}

void Link::frame(uint8_t type, uint8_t d, const uint8_t* p, uint32_t n) {
  if (type == LINK_HELLO) {
    // the box (re)started, or answered ours: either way, MOUNTs for what is plugged in follow
    box = true;
    version = n ? p[0] : 0;
    changed = true;
    for (uint8_t i = 0; i < LINK_DEVS; i++) {
      dev[i].on = false;
      drain(i);
    }
    // the box forgot ours
    for (uint8_t i = LINK_USB; i < LINK_DEVS; i++) {
      if (ownName[i - LINK_USB][0]) {
        mount(i, ownName[i - LINK_USB]);
      }
    }
    return;
  }
  if (d >= LINK_DEVS) {
    return;
  }
  box = true;
  if (type == LINK_MOUNT && n >= 4) {
    LinkDev& v = dev[d];
    v.vid = p[0] | p[1] << 8;
    v.pid = p[2] | p[3] << 8;
    uint32_t k = 0;
    for (uint32_t i = 4; i < n && k < sizeof v.name - 1; i++) {
      v.name[k++] = (char)p[i];
    }
    v.name[k] = 0;
    changed = true;
    if (!v.on) {
      v.on = true;
      v.serial = ++mounts;
      drain(d);
    }
  } else if (type == LINK_UMOUNT) {
    dev[d].on = false;
    changed = true;
    drain(d);
  } else if (type == LINK_MIDI) {
    for (uint32_t i = 0; i < n; i++) {
      midi(d, p[i]);
    }
  } else if (type == LINK_SEND && d >= LINK_USB && ownFn && ownName[d - LINK_USB][0]) {
    ownFn(d, p, n);
  }
}

void Link::own(LinkOwn fn) {
  ownFn = fn;
}

void Link::mount(uint8_t d, const char* name) {
  if (d < LINK_USB || d >= LINK_DEVS) {
    return;
  }
  char* own = ownName[d - LINK_USB];
  strlcpy(own, name[0] ? name : "?", sizeof ownName[0]);
  uint8_t p[4 + sizeof ownName[0]] = {0};
  uint32_t k = 4;
  for (const char* c = own; *c; c++) {
    p[k++] = (uint8_t)*c;
  }
  out(LINK_MOUNT, d, p, k);
}

void Link::umount(uint8_t d) {
  if (d >= LINK_USB && d < LINK_DEVS && ownName[d - LINK_USB][0]) {
    ownName[d - LINK_USB][0] = 0;
    out(LINK_UMOUNT, d, 0, 0);
  }
}

void Link::play(uint8_t d, const uint8_t* p, uint32_t n) {
  while (n) {
    uint32_t k = n > LINK_PAYLOAD ? LINK_PAYLOAD : n;
    out(LINK_MIDI, d, p, k);
    p += k;
    n -= k;
  }
}

void Link::midi(uint8_t d, uint8_t b) {
  if (b >= 0xF8) {
    return;  // realtime: may come anywhere, even inside a SysEx
  }
  // channel messages (with running status)
  if (b >= 0x80) {
    st[d] = b < 0xF0 ? b : 0;
    nd[d] = 0;
  } else if (st[d]) {
    data[d][nd[d]++] = b;
    if (nd[d] == ((st[d] >> 4) == 0xC || (st[d] >> 4) == 0xD ? 1 : 2)) {
      lastDev = d;
      last[0] = st[d];
      last[1] = data[d][0];
      last[2] = nd[d] > 1 ? data[d][1] : 0;
      seq++;
      nd[d] = 0;
    }
  }
  if (b == 0xF0) {
    sxOn[d] = true;
    sxn[d] = 0;
  }
  if (!sxOn[d]) {
    return;
  }
  if (b & 0x80 && b != 0xF0 && b != 0xF7) {
    sxOn[d] = false;  // another status ends it, unfinished
    return;
  }
  if (sxn[d] >= SYSEX_MAX) {
    sxOn[d] = false;  // too long to be one of ours
    return;
  }
  sx[d][sxn[d]++] = b;
  if (b == 0xF7) {
    sxOn[d] = false;
    if (qw[d] - qr[d] < SYSEX_Q) {
      uint32_t s = qw[d] % SYSEX_Q;
      memcpy(q[d][s], sx[d], sxn[d]);
      qn[d][s] = (uint8_t)sxn[d];
      qw[d]++;
    }
  }
}

uint32_t Link::sysex(uint8_t d, uint8_t* o) {
  if (d >= LINK_DEVS || qr[d] == qw[d]) {
    return 0;
  }
  uint32_t s = qr[d] % SYSEX_Q, n = qn[d][s];
  memcpy(o, q[d][s], n);
  qr[d]++;
  return n;
}

void Link::drain(uint8_t d) {
  qr[d] = qw[d];
  sxOn[d] = false;
}

void Link::send(uint8_t d, const uint8_t* p, uint32_t n) {
  while (n) {
    uint32_t k = n > LINK_PAYLOAD ? LINK_PAYLOAD : n;
    out(LINK_SEND, d, p, k);
    p += k;
    n -= k;
  }
}

void Link::route(const uint8_t r[LINK_DEVS]) {
  out(LINK_ROUTE, 0, r, LINK_DEVS);
}

void Link::map(const uint8_t* rules, uint32_t n) {
  out(LINK_MAP, 0, rules, n);
}

void Link::routeAll(bool on) {
  uint8_t r[LINK_DEVS];
  memset(r, on ? LINK_ROUTE_ALL : 0, sizeof r);
  route(r);
}
