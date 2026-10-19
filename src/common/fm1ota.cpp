#include "fm1ota.h"

#include "link.h"

static const uint8_t HS_QUERY[] = {0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7};
static const uint8_t UPGRADE[] = {0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7};
#define MAXDATA 512
#define FINISH_VERIFY 0xE0000000u
#define FINISH_WRITE 0xF0000000u

// ------------------------------------------------------------------------------------- the package ---
#define BLOCKS 20
#define BLK 0x30
#define KEEP 0x2F

bool Pkg::open(fs::FS& fs, const char* path) {
  f = fs.open(path, "r");
  if (!f || f.isDirectory() || f.size() < BLOCKS * BLK) {
    return false;
  }
  size = f.size() - BLOCKS;
  // the identity: one marker byte after each of the first 20 blocks
  uint8_t head[BLOCKS * BLK];
  if (f.read(head, sizeof head) != sizeof head) {
    return false;
  }
  uint32_t k = 0;
  for (int i = 0; i < BLOCKS; i++) {
    uint8_t m = head[i * BLK + KEEP];
    if (m != 0x7D && k < sizeof product - 1) {
      product[k++] = (char)((m - i - 1) & 0xFF);
    }
  }
  product[k] = 0;
  return k > 0;
}

bool Pkg::fits(const Fm1Id& id) const {
  const char* us = strchr(product, '_');
  size_t n = us ? us - product : strlen(product);
  return strlen(id.model) == n && !strncmp(id.model, product, n);
}

// the logical image: the first 20 blocks lose their last byte
bool Pkg::read(uint32_t addr, uint8_t* out, uint32_t len) {
  while (len && addr < BLOCKS * KEEP) {
    uint32_t i = addr / KEEP, o = addr % KEEP, n = KEEP - o;
    if (n > len) {
      n = len;
    }
    if (!f.seek(i * BLK + o) || f.read(out, n) != n) {
      return false;
    }
    addr += n;
    out += n;
    len -= n;
  }
  return !len || (f.seek(addr + BLOCKS) && f.read(out, len) == len);
}

// ---------------------------------------------------------------------------------------- framing ---
static uint32_t pack7(const uint8_t* in, uint32_t n, uint8_t* out) {
  uint32_t acc = 0, nb = 0, o = 0;
  while (n--) {
    acc |= (uint32_t)*in++ << nb;
    nb += 8;
    while (nb >= 7) {
      out[o++] = acc & 0x7F;
      acc >>= 7;
      nb -= 7;
    }
  }
  if (nb) {
    out[o++] = acc & 0x7F;
  }
  return o;
}

static uint32_t unpack7(const uint8_t* in, uint32_t n, uint8_t* out, uint32_t max) {
  uint32_t acc = 0, nb = 0, o = 0;
  while (n--) {
    acc |= (uint32_t)(*in++ & 0x7F) << nb;
    nb += 7;
    while (nb >= 8) {
      if (o < max) {
        out[o] = acc & 0xFF;
      }
      o++;
      acc >>= 8;
      nb -= 8;
    }
  }
  return o;
}

static void respond(uint8_t d, uint32_t addr, const uint8_t* data, uint32_t len, uint8_t fl) {
  static uint8_t body[14 + MAXDATA + 1], wire[2 + (sizeof body * 8 + 6) / 7];
  uint32_t n = 0, s = 0;
  body[n++] = 0x00;
  body[n++] = 0x59;
  body[n++] = 0x30;
  body[n++] = (len + 8) & 0xFF;
  body[n++] = ((len + 8) >> 8) & 0xFF;
  body[n++] = 0;
  body[n++] = fl;
  body[n++] = addr & 0xFF;
  body[n++] = (addr >> 8) & 0xFF;
  body[n++] = (addr >> 16) & 0xFF;
  body[n++] = (addr >> 24) & 0xFF;
  body[n++] = len & 0xFF;
  body[n++] = (len >> 8) & 0xFF;
  body[n++] = 0;
  memcpy(body + n, data, len);
  n += len;
  for (uint32_t i = 6; i < n; i++) {
    s += body[i];
  }
  body[n++] = ~s & 0xFF;
  uint32_t w = 0;
  wire[w++] = 0xF0;
  w += pack7(body, n, wire + w);
  wire[w++] = 0xF7;
  hub.send(d, wire, w);
}

struct Request {
  uint8_t fl;
  uint32_t addr, len;
};

static bool parseRequest(const uint8_t* pkt, uint32_t n, Request& r) {
  uint8_t u[15];
  if (n < 2 || pkt[0] != 0xF0 || pkt[n - 1] != 0xF7 || unpack7(pkt + 1, n - 2, u, sizeof u) != 15) {
    return false;
  }
  if (u[0] != 0 || u[1] != 0x59 || u[2] != 0x30) {
    return false;
  }
  uint32_t s = 0;
  for (int i = 6; i < 14; i++) {
    s += u[i];
  }
  if ((~s & 0xFF) != u[14]) {
    return false;
  }
  r.fl = u[6];
  r.addr = u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24;
  r.len = u[11] | u[12] << 8 | u[13] << 16;
  return true;
}

static bool parseIdentity(const uint8_t* pkt, uint32_t n, Fm1Id& id) {
  uint8_t d[34];
  if (n < 2 || pkt[0] != 0xF0 || pkt[n - 1] != 0xF7 || unpack7(pkt + 1, n - 2, d, sizeof d) != 34) {
    return false;
  }
  if (d[0] != 0 || d[1] != 0x59 || d[2] != 0x11) {
    return false;
  }
  uint32_t k = 0;
  for (int i = 6; i < 33 && d[i]; i++) {
    id.text[k++] = (char)d[i];
  }
  id.text[k] = 0;
  // MODEL_123
  char* us = strrchr(id.text, '_');
  if (!us || us == id.text || !us[1] || us - id.text >= (int)sizeof id.model) {
    return false;
  }
  for (char* c = us + 1; *c; c++) {
    if (*c < '0' || *c > '9') {
      return false;
    }
  }
  memcpy(id.model, id.text, us - id.text);
  id.model[us - id.text] = 0;
  id.version = atoi(us + 1);
  return true;
}

// ------------------------------------------------------------------------------------------ device ---
void fm1::wait(uint32_t ms) {
  uint32_t t = millis();
  do {
    hub.poll();
    delay(1);
  } while (millis() - t < ms);
}

bool fm1::candidate(uint8_t d) {
  static const char* NAMES[] = {"fm-1", "felucca", "ota", "composite", "sinco", "usb-midi"};
  if (d >= LINK_USB || !hub.dev[d].on) {  // installing needs USB
    return false;
  }
  char lower[sizeof hub.dev[d].name];
  uint32_t i = 0;
  for (; hub.dev[d].name[i]; i++) {
    lower[i] = tolower(hub.dev[d].name[i]);
  }
  lower[i] = 0;
  for (const char* n : NAMES) {
    if (strstr(lower, n)) {
      return true;
    }
  }
  return hub.dev[d].vid == 0x1209 && (hub.dev[d].pid == 1 || hub.dev[d].pid == 2);  // Felucca, its loader
}

bool fm1::handshake(uint8_t d, Fm1Id& id, int tries) {
  uint8_t pkt[SYSEX_MAX];
  hub.drain(d);
  for (int t = 0; t < tries; t++) {
    hub.send(d, HS_QUERY, sizeof HS_QUERY);
    uint32_t start = millis();
    while (millis() - start < 1000) {
      hub.poll();
      if (!hub.dev[d].on) {
        return false;
      }
      uint32_t n = hub.sysex(d, pkt);
      if (n && parseIdentity(pkt, n, id)) {
        return true;
      }
      delay(1);
    }
  }
  return false;
}

int fm1::find(bool ota, Fm1Id& id) {
  for (uint8_t d = 0; d < LINK_DEVS; d++) {
    if (candidate(d) && handshake(d, id) && id.ota() == ota) {
      return d;
    }
  }
  return -1;
}

static struct {
  uint32_t serial;  // the mount it was asked on (0: not yet)
  uint32_t askAt;
  bool ok;
  Fm1Id id;
} ids[LINK_DEVS];

bool fm1::watch() {
  bool changed = false;
  for (int d = 0; d < LINK_DEVS; d++) {
    if (!hub.dev[d].on) {
      if (ids[d].serial) {
        ids[d].serial = 0;
        ids[d].ok = false;
        changed = true;
      }
      continue;
    }
    if (ids[d].serial == hub.dev[d].serial) {
      continue;
    }
    if (!ids[d].askAt) {
      ids[d].askAt = millis();
      changed = true;  // a new device: the list changes
    } else if (millis() - ids[d].askAt > 1500) {
      ids[d].serial = hub.dev[d].serial;
      ids[d].askAt = 0;
      ids[d].ok = candidate(d) && handshake(d, ids[d].id);
      changed = true;
    }
  }
  return changed;
}

const Fm1Id* fm1::known(int d) {
  return d >= 0 && d < LINK_DEVS && hub.dev[d].on && ids[d].ok && ids[d].serial == hub.dev[d].serial ? &ids[d].id : 0;
}

int fm1::current() {
  for (int d = 0; d < LINK_DEVS; d++) {
    if (known(d)) {
      return d;
    }
  }
  return -1;
}

void fm1::forget() {
  memset(ids, 0, sizeof ids);
}

static int waitFor(bool ota, Fm1Id& id, uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    int d = fm1::find(ota, id);
    if (d >= 0) {
      return d;
    }
    fm1::wait(1000);
  }
  return -1;
}

struct Served {
  uint32_t served;
  bool finished, lost;
};

// answer read requests until the device asks for the finish address; stops early when the device goes
static Served serve(uint8_t d, Pkg& pkg, uint32_t finish, uint32_t idleMs, Fm1Step step, Fm1Progress cb) {
  static uint8_t data[MAXDATA];
  static const char OK[] = "success";
  uint8_t pkt[SYSEX_MAX];
  uint32_t serial = hub.dev[d].serial, last = millis();
  Served s = {0, false, false};
  for (;;) {
    hub.poll();
    if (!hub.dev[d].on || hub.dev[d].serial != serial) {
      s.lost = true;
      return s;
    }
    uint32_t n = hub.sysex(d, pkt);
    Request r;
    if (!n || !parseRequest(pkt, n, r)) {
      if (millis() - last > idleMs) {
        return s;
      }
      delay(1);
      continue;
    }
    last = millis();
    if (r.addr == FINISH_VERIFY || r.addr == FINISH_WRITE) {
      respond(d, r.addr, (const uint8_t*)OK, sizeof OK, 0);  // with its 0
      if (r.addr == finish) {
        s.finished = true;
        return s;
      }
      continue;
    }
    if (r.len > MAXDATA || r.addr + r.len > pkg.size || !pkg.read(r.addr, data, r.len)) {
      return s;  // not finished: a bad request
    }
    fm1::wait(10);
    respond(d, r.addr, data, r.len, r.fl);
    s.served++;
    if (cb) {
      cb(step, s.served, r.addr);
    }
  }
}

static const char* write(uint8_t d, Pkg& pkg, Fm1Progress cb) {
  hub.send(d, UPGRADE, sizeof UPGRADE);
  fm1::wait(2000);
  Served s = serve(d, pkg, FINISH_WRITE, 180000, STEP_WRITE, cb);
  if (!s.finished) {
    return s.lost ? "The loader was disconnected. Replug the FM-1 and install again to resume."
                  : "The loader stopped. Replug the FM-1 and install again to resume.";
  }
  return 0;
}

static const char* comeBack(Pkg& pkg, Fm1Progress cb) {
  static char err[80];
  Fm1Id id;
  cb(STEP_REBOOT, 0, 0);
  fm1::wait(3000);
  if (waitFor(false, id, 40000) < 0) {
    return "The FM-1 did not come back: power-cycle it.";
  }
  if (strcmp(id.text, pkg.product)) {
    snprintf(err, sizeof err, "Installed, but the FM-1 reports %s.", id.text);
    return err;
  }
  cb(STEP_DONE, 0, 0);
  return 0;
}

static const char* installSteps(Pkg& pkg, Fm1Progress cb) {
  static char err[80];
  Fm1Id id;
  int d = fm1::find(false, id);
  if (d < 0) {
    return "FM-1 not found: is it plugged into the host box?";
  }
  if (!pkg.fits(id)) {
    snprintf(err, sizeof err, "The device is %s, the package is for %s.", id.text, pkg.product);
    return err;
  }
  cb(STEP_START, 0, 0);
  hub.send(d, UPGRADE, sizeof UPGRADE);
  fm1::wait(2000);
  Served s = serve(d, pkg, FINISH_VERIFY, 8000, STEP_VERIFY, cb);
  if (!s.finished) {
    snprintf(err, sizeof err, "The FM-1 %s after %u requests: nothing was written.",
             s.lost ? "was disconnected" : "stopped", (unsigned)s.served);
    return err;
  }
  cb(STEP_LOADER, 0, 0);
  fm1::wait(3000);
  d = waitFor(true, id, 30000);
  if (d < 0) {
    return "The update loader did not appear. Replug the FM-1 and install again: it stays in update mode until it is finished.";
  }
  const char* e = write(d, pkg, cb);
  return e ? e : comeBack(pkg, cb);
}

// the FM-1's update stays between it and us: nothing routed while it runs
const char* fm1::install(Pkg& pkg, Fm1Progress cb) {
  hub.routeAll(false);
  const char* e = installSteps(pkg, cb);
  hub.routeAll(true);
  return e;
}

const char* fm1::resume(Pkg& pkg, Fm1Progress cb) {
  Fm1Id id;
  hub.routeAll(false);
  int d = find(true, id);
  const char* e = d < 0 ? "No FM-1 in update mode found." : write(d, pkg, cb);
  if (!e) {
    e = comeBack(pkg, cb);
  }
  hub.routeAll(true);
  return e;
}
