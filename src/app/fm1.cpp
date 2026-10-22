#include "fm1.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <chrono>
#include <thread>
#include <vector>

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
char imagePath[160];
}  // namespace fm1

bool Fm1Id::ota() const {
  return strncasecmp(model, "ota-", 4) == 0;
}

// ------------------------------------------------------------------------------------- the package ---
#define BLOCKS 20
#define BLK 0x30
#define KEEP 0x2F

// the whole file into memory, read twice (the second time just for its sha256): a card that reads differently
// each time is found before the FM-1 is touched
const char* Pkg::open(const char* p) {
  close();
  snprintf(path, sizeof path, "%s", p);
  FILE* f = fopen(p, "rb");
  if (!f) {
    return "Can't open the file on the SD card.";
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n < BLOCKS * BLK || n > 16 * 1024 * 1024) {
    fclose(f);
    return "That isn't an FM-1 package.";
  }
  image = (uint8_t*)malloc(n);  // big blocks go to the PSRAM
  if (!image) {
    fclose(f);
    return "Not enough memory for the package.";
  }
  bool got = fread(image, 1, n, f) == (size_t)n;
  fclose(f);
  if (!got) {
    close();
    return "The SD card didn't read the whole file.";
  }
  fileSize = n;
  char again[65];
  if (!hal::sha256(image, n, sha256) || !hal::sha256(p, again) || strcmp(sha256, again)) {
    close();
    return "The SD card read the file differently twice: try another card.";
  }
  // the identity: one marker byte after each of the first 20 blocks
  uint32_t k = 0;
  for (int i = 0; i < BLOCKS; i++) {
    uint8_t m = image[i * BLK + KEEP];
    if (m != 0x7D && k < sizeof product - 1) {
      product[k++] = (char)((m - i - 1) & 0xFF);
    }
  }
  product[k] = 0;
  if (!k) {
    close();
    return "That isn't an FM-1 package.";
  }
  // the logical image: the first 20 blocks lose their last byte
  for (int i = 1; i < BLOCKS; i++) {
    memmove(image + i * KEEP, image + i * BLK, KEEP);
  }
  memmove(image + BLOCKS * KEEP, image + BLOCKS * BLK, n - BLOCKS * BLK);
  size = n - BLOCKS;
  return 0;
}

void Pkg::close() {
  free(image);
  image = 0;
  size = 0;
}

bool Pkg::fits(const Fm1Id& id) const {
  const char* us = strchr(product, '_');
  size_t n = us ? us - product : strlen(product);
  return strlen(id.model) == n && !strncmp(id.model, product, n);
}

bool Pkg::read(uint32_t addr, uint8_t* out, uint32_t len) const {
  if (!image || addr > size || len > size - addr) {
    return false;
  }
  memcpy(out, image + addr, len);
  return true;
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

// false: the device went, or didn't take it for a second
static bool respond(int d, uint32_t addr, const uint8_t* data, uint32_t len, uint8_t fl) {
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
  return midi::sendWait(d, wire, w, 1000);
}

// (each call its own buffer: a job and the UI may both be at it)
uint32_t fm1::frame(uint8_t cmd, const uint8_t* payload, uint32_t n, uint8_t* out) {
  if (n > 1024) {
    return 0;
  }
  std::vector<uint8_t> buf(7 + n);
  uint8_t* body = buf.data();
  uint32_t k = 0, sum = 0;
  body[k++] = 0x00;
  body[k++] = 0x59;
  body[k++] = cmd;
  body[k++] = n & 0xFF;
  body[k++] = (n >> 8) & 0xFF;
  body[k++] = (n >> 16) & 0xFF;
  for (uint32_t i = 0; i < n; i++) {
    sum += payload[i];
    body[k++] = payload[i];
  }
  body[k++] = ~sum & 0xFF;
  uint32_t w = 0;
  out[w++] = 0xF0;
  w += pack7(body, k, out + w);
  out[w++] = 0xF7;
  return w;
}

int fm1::unframe(const uint8_t* pkt, uint32_t n, uint8_t cmd, uint8_t* out, uint32_t max) {
  if (n < 10 || n > 2 + (7 + 1024) * 8 / 7 + 1 || pkt[0] != 0xF0 || pkt[n - 1] != 0xF7) {
    return -1;
  }
  std::vector<uint8_t> buf(7 + 1024);
  uint8_t* body = buf.data();
  uint32_t k = unpack7(pkt + 1, n - 2, body, buf.size());
  if (k < 7 || k > buf.size() || body[0] || body[1] != 0x59 || body[2] != cmd) {
    return -1;
  }
  uint32_t len = body[3] | body[4] << 8 | body[5] << 16, sum = 0;
  if (len + 7 > k) {
    return -1;
  }
  for (uint32_t i = 0; i < len; i++) {
    sum += body[6 + i];
  }
  if ((~sum & 0xFF) != body[6 + len] || len > max) {
    return -1;
  }
  memcpy(out, body + 6, len);
  return len;
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
static bool candidateDev(const midi::Dev& v) {
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

bool fm1::candidate(int d) {
  midi::Dev v;
  return midi::dev(d, v) && candidateDev(v);
}

static int find(bool ota, Fm1Id& id) {
  midi::Dev v;
  for (int d = 0; d < MIDI_DEVS; d++) {
    if (midi::dev(d, v) && candidateDev(v) && handshake(d, id) && id.ota() == ota) {
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
      s.done = !candidateDev(v);
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

// ------------------------------------------------------------------------------------------- record ---
static FILE* rec;
static uint32_t recStart;

static void recLine(bool flush, const char* fmt, va_list a) {
  if (!rec) {
    return;
  }
  fprintf(rec, "%7.3f  ", (hal::millis() - recStart) / 1000.0);
  vfprintf(rec, fmt, a);
  fputc('\n', rec);
  if (flush) {
    fflush(rec);  // what happened so far is on the card, even if the power goes
  }
}

// a step, an error, anything odd
static void note(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void note(const char* fmt, ...) {
  va_list a;
  va_start(a, fmt);
  recLine(true, fmt, a);
  va_end(a);
}

// a request (there are thousands: written in chunks)
static void noteReq(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void noteReq(const char* fmt, ...) {
  va_list a;
  va_start(a, fmt);
  recLine(false, fmt, a);
  va_end(a);
}

static void noteHex(const char* what, const uint8_t* p, uint32_t n) {
  if (!rec) {
    return;
  }
  char hex[3 * SYSEX_MAX + 1];
  uint32_t k = 0;
  for (uint32_t i = 0; i < n && i < SYSEX_MAX; i++) {
    k += snprintf(hex + k, sizeof hex - k, "%02X ", p[i]);
  }
  hex[k ? k - 1 : 0] = 0;
  note("! %s (%u bytes): %s", what, (unsigned)n, hex);
}

// ------------------------------------------------------------------------------------------ install ---
struct Served {
  uint32_t served;
  bool finished, lost, bad;
};

// answer read requests until the device asks for the finish address; stops early when the device goes
static Served serve(int d, const Pkg& pkg, uint32_t finish, uint32_t idleMs, Fm1Step step, Fm1Progress cb) {
  static uint8_t data[MAXDATA];
  static const char OK[] = "success";
  uint8_t pkt[SYSEX_MAX];
  midi::Dev v;
  midi::dev(d, v);
  uint32_t serial = v.serial, last = hal::millis(), dropped = v.dropped, lost = v.sysexLost;
  uint32_t prevAddr = ~0u, prevLen = 0, next = 0, again = 0, jumps = 0, odd = 0;
  Served s = {0, false, false, false};
  auto summary = [&](const char* how) {
    note("%s: %u answered, %u asked again, %u out of order, %u not understood", how, (unsigned)s.served,
         (unsigned)again, (unsigned)jumps, (unsigned)odd);
  };
  for (;;) {
    if (!midi::dev(d, v) || v.serial != serial) {
      s.lost = true;
      summary("the device went");
      return s;
    }
    if (v.dropped != dropped || v.sysexLost != lost) {
      note("! lost on the way: %u to it, %u from it", (unsigned)(v.dropped - dropped), (unsigned)(v.sysexLost - lost));
      dropped = v.dropped;
      lost = v.sysexLost;
    }
    uint32_t n = midi::sysex(d, pkt);
    Request r;
    if (!n) {
      if (hal::millis() - last > idleMs) {
        summary("nothing asked for a while");
        return s;
      }
      wait(1);
      continue;
    }
    if (!parseRequest(pkt, n, r)) {
      odd++;
      noteHex("not a request", pkt, n);
      continue;
    }
    last = hal::millis();
    if (r.addr == FINISH_VERIFY || r.addr == FINISH_WRITE) {
      note("finish %08X asked", (unsigned)r.addr);
      if (!respond(d, r.addr, (const uint8_t*)OK, sizeof OK, 0)) {  // with its 0
        note("! the answer didn't go");
      }
      if (r.addr == finish) {
        s.finished = true;
        summary("finished");
        return s;
      }
      continue;
    }
    const char* tag = "";
    if (r.addr == prevAddr && r.len == prevLen) {
      tag = "  again";  // our answer was lost, or late
      again++;
    } else if (step == STEP_WRITE && s.served && r.addr != next) {
      tag = "  jump";
      jumps++;
    }
    noteReq("%08X+%u fl %u%s", (unsigned)r.addr, (unsigned)r.len, r.fl, tag);
    prevAddr = r.addr;
    prevLen = r.len;
    next = r.addr + r.len;
    if (r.len > MAXDATA || !pkg.read(r.addr, data, r.len)) {
      note("! a request past the package (%u bytes)", (unsigned)pkg.size);
      hal::log("fm1: a bad request: %08X+%u", (unsigned)r.addr, (unsigned)r.len);
      s.bad = true;
      summary("stopped");
      return s;
    }
    wait(10);
    if (!respond(d, r.addr, data, r.len, r.fl)) {
      note("! the answer didn't go");
      continue;  // it asks again, or goes
    }
    s.served++;
    if (cb) {
      cb(step, s.served, r.addr);
    }
  }
}

static bool upgrade(int d) {
  midi::drain(d);
  bool sent = midi::sendWait(d, UPGRADE, sizeof UPGRADE, 1000);
  note(sent ? "upgrade sent" : "! upgrade didn't go");
  return sent;
}

static const char* write(int d, const Pkg& pkg, Fm1Progress cb) {
  cb(STEP_WRITE, 0, 0);
  note("writing");
  upgrade(d);
  wait(2000);
  Served s = serve(d, pkg, FINISH_WRITE, 180000, STEP_WRITE, cb);
  if (!s.finished) {
    return s.lost ? "The loader was disconnected. Replug the FM-1 and install again to finish."
                  : "The loader stopped. Replug the FM-1 and install again to finish.";
  }
  return 0;
}

static const char* comeBack(const Pkg& pkg, Fm1Progress cb) {
  static char err[80];
  Fm1Id id;
  cb(STEP_REBOOT, 0, 0);
  note("waiting for it to restart");
  wait(3000);
  int d = waitFor(false, id, 40000);
  if (d < 0) {
    return "The FM-1 didn't come back: switch it off and on.";
  }
  note("back: %d says %s", d, id.text);
  if (strcmp(id.text, pkg.product)) {
    snprintf(err, sizeof err, "Installed, but the FM-1 says it's %s.", id.text);
    return err;
  }
  cb(STEP_DONE, 0, 0);
  return 0;
}

static const char* steps(const Pkg& pkg, Fm1Progress cb) {
  static char err[96];
  Fm1Id id;
  cb(STEP_START, 0, 0);
  int d = find(true, id);
  if (d >= 0) {  // in update mode already: finish it
    hal::log("fm1: %s is waiting: finishing", id.text);
    note("%d is %s, in update mode: finishing", d, id.text);
    const char* e = write(d, pkg, cb);
    return e ? e : comeBack(pkg, cb);
  }
  d = find(false, id);
  if (d < 0) {
    return "No FM-1 answered: is it on, and on the hub?";
  }
  midi::Dev v;
  midi::dev(d, v);
  note("%d is %s (%s, %04X:%04X)", d, id.text, v.name, v.vid, v.pid);
  if (!pkg.fits(id)) {
    snprintf(err, sizeof err, "The FM-1 is %s, the package is for %s.", id.text, pkg.product);
    return err;
  }
  cb(STEP_VERIFY, 0, 0);
  note("verifying");
  upgrade(d);
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
  note("waiting for the loader");
  wait(3000);
  d = waitFor(true, id, 30000);
  if (d < 0) {
    return "The update loader didn't appear. Replug the FM-1 and install again: it stays in update mode until it's done.";
  }
  note("%d is %s", d, id.text);
  const char* e = write(d, pkg, cb);
  return e ? e : comeBack(pkg, cb);
}

// the update stays between the FM-1 and us: nothing goes through while it runs
const char* fm1::install(const Pkg& pkg, Fm1Progress cb, FILE* r) {
  busy = true;
  midi::thru = false;
  imageSize = pkg.size;
  snprintf(imageProduct, sizeof imageProduct, "%s", pkg.product);
  snprintf(imagePath, sizeof imagePath, "%s", pkg.path);
  rec = r;
  recStart = hal::millis();
  note("looking for the FM-1");
  const char* e = steps(pkg, cb);
  note("%s", e ? e : "done");
  rec = 0;
  hal::log("fm1: install: %s", e ? e : "done");
  memset(ids, 0, sizeof ids);  // ask them all again
  midi::thru = true;
  busy = false;
  return e;
}
