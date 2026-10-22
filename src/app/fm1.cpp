#include "fm1.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <chrono>
#include <thread>

#include "hal.h"
#include "midi.h"

static const uint8_t HS_QUERY[] = {0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7};
static const uint8_t UPGRADE[] = {0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7};
#define MAXDATA 512
#define FINISH_VERIFY 0xE0000000u
#define FINISH_WRITE 0xF0000000u

namespace fm1 {
std::atomic<bool> busy{false};
std::atomic<uint32_t> imageSize{0};
char imageProduct[24];
}  // namespace fm1

bool Fm1Id::ota() const {
  return strncasecmp(model, "ota-", 4) == 0;
}

// ------------------------------------------------------------------------------------- the package ---
#define BLOCKS 20
#define BLK 0x30
#define KEEP 0x2F

bool Pkg::open(const char* path) {
  close();
  f = fopen(path, "rb");
  if (!f) {
    return false;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  if (n < BLOCKS * BLK) {
    return false;
  }
  size = n - BLOCKS;
  // the identity: one marker byte after each of the first 20 blocks
  uint8_t head[BLOCKS * BLK];
  fseek(f, 0, SEEK_SET);
  if (fread(head, 1, sizeof head, f) != sizeof head) {
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

void Pkg::close() {
  if (f) {
    fclose(f);
    f = 0;
  }
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
    if (fseek(f, i * BLK + o, SEEK_SET) || fread(out, 1, n, f) != n) {
      return false;
    }
    addr += n;
    out += n;
    len -= n;
  }
  return !len || (!fseek(f, addr + BLOCKS, SEEK_SET) && fread(out, 1, len, f) == len);
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

static void respond(int d, uint32_t addr, const uint8_t* data, uint32_t len, uint8_t fl) {
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
  midi::send(d, wire, w);
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

// ------------------------------------------------------------------------------------------ devices ---
static void wait(uint32_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// the device can be an FM-1: by its USB name (never probe other gear)
static bool candidate(const midi::Dev& v) {
  static const char* NAMES[] = {"fm-1", "felucca", "ota", "composite", "sinco", "usb-midi"};
  char lower[sizeof v.name];
  uint32_t i = 0;
  for (; v.name[i]; i++) {
    lower[i] = tolower(v.name[i]);
  }
  lower[i] = 0;
  for (const char* n : NAMES) {
    if (strstr(lower, n)) {
      return true;
    }
  }
  return v.vid == 0x1209 && (v.pid == 1 || v.pid == 2);  // Felucca, its loader
}

static bool handshake(int d, Fm1Id& id, int tries = 2) {
  uint8_t pkt[SYSEX_MAX];
  midi::drain(d);
  midi::Dev v;
  for (int t = 0; t < tries; t++) {
    midi::send(d, HS_QUERY, sizeof HS_QUERY);
    for (uint32_t start = hal::millis(); hal::millis() - start < 1000; wait(2)) {
      if (!midi::dev(d, v)) {
        return false;
      }
      uint32_t n = midi::sysex(d, pkt);
      if (n && parseIdentity(pkt, n, id)) {
        return true;
      }
    }
  }
  return false;
}

static int find(bool ota, Fm1Id& id) {
  midi::Dev v;
  for (int d = 0; d < MIDI_DEVS; d++) {
    if (midi::dev(d, v) && candidate(v) && handshake(d, id) && id.ota() == ota) {
      return d;
    }
  }
  return -1;
}

static int waitFor(bool ota, Fm1Id& id, uint32_t ms) {
  for (uint32_t start = hal::millis(); hal::millis() - start < ms; wait(1000)) {
    int d = find(ota, id);
    if (d >= 0) {
      return d;
    }
  }
  return -1;
}

// what each device said, asked once it settled (not blocking: fm1::watch)
static struct {
  uint32_t serial;  // the mount it's about (0: none)
  uint32_t askAt, sentAt;
  uint8_t tries;
  bool done, ok;
  Fm1Id id;
} ids[MIDI_DEVS];

void fm1::watch() {
  if (busy) {
    return;
  }
  uint32_t now = hal::millis();
  for (int d = 0; d < MIDI_DEVS; d++) {
    auto& s = ids[d];
    midi::Dev v;
    if (!midi::dev(d, v)) {
      s = {};
      continue;
    }
    if (s.serial != v.serial) {
      s = {};
      s.serial = v.serial;
      s.askAt = now + 1500;  // it settles first
      s.done = !candidate(v);
      continue;
    }
    if (s.done) {
      continue;
    }
    uint8_t pkt[SYSEX_MAX];
    if (!s.sentAt) {
      if ((int32_t)(now - s.askAt) >= 0) {
        midi::drain(d);
        midi::send(d, HS_QUERY, sizeof HS_QUERY);
        s.sentAt = now;
        s.tries++;
      }
    } else if (uint32_t n = midi::sysex(d, pkt)) {
      if (parseIdentity(pkt, n, s.id)) {
        s.ok = s.done = true;
        midi::seq++;
        hal::log("fm1: %d is %s", d, s.id.text);
      }
    } else if (now - s.sentAt > 1000) {
      s.sentAt = 0;
      s.askAt = now;
      s.done = s.tries >= 2;
    }
  }
}

const Fm1Id* fm1::known(int d) {
  midi::Dev v;
  return d >= 0 && d < MIDI_DEVS && midi::dev(d, v) && ids[d].ok && ids[d].serial == v.serial ? &ids[d].id : 0;
}

int fm1::current() {
  for (int d = 0; d < MIDI_DEVS; d++) {
    if (known(d)) {
      return d;
    }
  }
  return -1;
}

// ------------------------------------------------------------------------------------------ install ---
struct Served {
  uint32_t served;
  bool finished, lost, bad;
};

// answer read requests until the device asks for the finish address; stops early when the device goes
static Served serve(int d, Pkg& pkg, uint32_t finish, uint32_t idleMs, Fm1Step step, Fm1Progress cb) {
  static uint8_t data[MAXDATA];
  static const char OK[] = "success";
  uint8_t pkt[SYSEX_MAX];
  midi::Dev v;
  midi::dev(d, v);
  uint32_t serial = v.serial, last = hal::millis();
  Served s = {0, false, false, false};
  for (;;) {
    if (!midi::dev(d, v) || v.serial != serial) {
      s.lost = true;
      return s;
    }
    uint32_t n = midi::sysex(d, pkt);
    Request r;
    if (!n || !parseRequest(pkt, n, r)) {
      if (hal::millis() - last > idleMs) {
        return s;
      }
      wait(1);
      continue;
    }
    last = hal::millis();
    if (r.addr == FINISH_VERIFY || r.addr == FINISH_WRITE) {
      respond(d, r.addr, (const uint8_t*)OK, sizeof OK, 0);  // with its 0
      if (r.addr == finish) {
        s.finished = true;
        return s;
      }
      continue;
    }
    if (r.len > MAXDATA || r.addr + r.len > pkg.size || !pkg.read(r.addr, data, r.len)) {
      hal::log("fm1: a bad request: %08X+%u", (unsigned)r.addr, (unsigned)r.len);
      s.bad = true;
      return s;
    }
    wait(10);
    respond(d, r.addr, data, r.len, r.fl);
    s.served++;
    if (cb) {
      cb(step, s.served, r.addr);
    }
  }
}

static const char* write(int d, Pkg& pkg, Fm1Progress cb) {
  cb(STEP_WRITE, 0, 0);
  midi::drain(d);
  midi::send(d, UPGRADE, sizeof UPGRADE);
  wait(2000);
  Served s = serve(d, pkg, FINISH_WRITE, 180000, STEP_WRITE, cb);
  if (!s.finished) {
    return s.lost ? "The loader was disconnected. Replug the FM-1 and install again to finish."
                  : "The loader stopped. Replug the FM-1 and install again to finish.";
  }
  return 0;
}

static const char* comeBack(Pkg& pkg, Fm1Progress cb) {
  static char err[80];
  Fm1Id id;
  cb(STEP_REBOOT, 0, 0);
  wait(3000);
  if (waitFor(false, id, 40000) < 0) {
    return "The FM-1 didn't come back: switch it off and on.";
  }
  if (strcmp(id.text, pkg.product)) {
    snprintf(err, sizeof err, "Installed, but the FM-1 says it's %s.", id.text);
    return err;
  }
  cb(STEP_DONE, 0, 0);
  return 0;
}

static const char* steps(Pkg& pkg, Fm1Progress cb) {
  static char err[96];
  Fm1Id id;
  cb(STEP_START, 0, 0);
  int d = find(true, id);
  if (d >= 0) {  // in update mode already: finish it
    hal::log("fm1: %s is waiting: finishing", id.text);
    const char* e = write(d, pkg, cb);
    return e ? e : comeBack(pkg, cb);
  }
  d = find(false, id);
  if (d < 0) {
    return "No FM-1 answered: is it on, and on the hub?";
  }
  if (!pkg.fits(id)) {
    snprintf(err, sizeof err, "The FM-1 is %s, the package is for %s.", id.text, pkg.product);
    return err;
  }
  cb(STEP_VERIFY, 0, 0);
  midi::drain(d);
  midi::send(d, UPGRADE, sizeof UPGRADE);
  wait(2000);
  Served s = serve(d, pkg, FINISH_VERIFY, 8000, STEP_VERIFY, cb);
  if (!s.finished) {
    snprintf(err, sizeof err, "The FM-1 %s after %u requests: nothing was written.",
             s.lost ? "was disconnected" : s.bad ? "asked for something not in the package"
                                                 : "stopped",
             (unsigned)s.served);
    return err;
  }
  cb(STEP_LOADER, 0, 0);
  wait(3000);
  d = waitFor(true, id, 30000);
  if (d < 0) {
    return "The update loader didn't appear. Replug the FM-1 and install again: it stays in update mode until it's done.";
  }
  const char* e = write(d, pkg, cb);
  return e ? e : comeBack(pkg, cb);
}

// the update stays between the FM-1 and us: nothing goes through while it runs
const char* fm1::install(Pkg& pkg, Fm1Progress cb) {
  busy = true;
  midi::thru = false;
  imageSize = pkg.size;
  snprintf(imageProduct, sizeof imageProduct, "%s", pkg.product);
  const char* e = steps(pkg, cb);
  hal::log("fm1: install: %s", e ? e : "done");
  memset(ids, 0, sizeof ids);  // ask them all again
  midi::thru = true;
  busy = false;
  return e;
}
