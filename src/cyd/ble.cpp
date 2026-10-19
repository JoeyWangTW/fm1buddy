#include "ble.h"

#include <NimBLEDevice.h>
#include <Preferences.h>
#include <freertos/ringbuf.h>

#include "fm1map.h"
#include "link.h"

#define SLOTS (LINK_DEVS - LINK_USB)
#define PKT 128       // the longest packet we send
#define EV_DATA 512   // the longest packet we take
#define AROUND 25000  // found in the last 25 s

BlePeer ble::peers[BLE_PEERS];
int ble::count;
using ble::count;
using ble::peers;

static const NimBLEUUID SVC("03b80e5a-ede8-4b33-a751-6ce34ec4c700");  // BLE MIDI
static const NimBLEUUID CHR("7772e5db-3868-4112-a1a9-f2669d106bf3");  // its MIDI I/O

// what the NimBLE task saw, for the main loop: an Ev, then its data
enum { EV_HOST_ON,
       EV_HOST_OFF,
       EV_HOST_DATA,
       EV_PEER_ON,
       EV_PEER_OFF,
       EV_PEER_DATA,
       EV_FOUND };
struct Ev {
  uint8_t type, who;
  uint16_t conn;
};

static RingbufHandle_t evq;
static Preferences prefs;
static bool on, searching, dirty;
static uint32_t scanAt;
static NimBLEServer* server;
static NimBLECharacteristic* chr;

// the CYD's devices on the box: device LINK_USB + slot
enum { FREE,
       HOST,
       PEER };
static struct {
  uint8_t kind;
  int8_t cl;  // PEER: its client
  uint16_t conn;
  // what the box sends it, into BLE MIDI packets
  uint8_t pkt[PKT];
  uint16_t n, max;
  uint8_t st, need, have, msg[2];
  bool sx;
} slots[SLOTS];

// connections to keyboards and synths
enum { IDLE,
       CONNECTING,
       SETUP,  // connected: find its MIDI
       READY,
       CLOSING };
static struct {
  NimBLEClient* c;
  int8_t peer, slot;
  uint8_t st;
  uint32_t at;
  NimBLERemoteCharacteristic* ch;
} cls[BLE_SAVED];

// ------------------------------------------------------------------------------- from the NimBLE task ---
static void ev(uint8_t type, uint8_t who, uint16_t conn = 0, const uint8_t* p = 0, size_t n = 0) {
  static uint8_t b[sizeof(Ev) + EV_DATA];  // only ever the NimBLE task
  Ev e = {type, who, conn};
  n = n > EV_DATA ? EV_DATA : n;
  memcpy(b, &e, sizeof e);
  if (n) {
    memcpy(b + sizeof e, p, n);
  }
  xRingbufferSend(evq, b, sizeof e + n, 0);  // full: lost
}

static int clientOf(const NimBLEClient* c) {
  for (int k = 0; k < BLE_SAVED; k++) {
    if (cls[k].c == c) {
      return k;
    }
  }
  return -1;
}

class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    ev(EV_HOST_ON, 0, info.getConnHandle());
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
    ev(EV_HOST_OFF, 0, info.getConnHandle());
  }
};

class CharCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    const NimBLEAttValue& v = c->getValue();
    ev(EV_HOST_DATA, 0, info.getConnHandle(), v.data(), v.length());
  }
};

class ClientCB : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* c) override {
    ev(EV_PEER_ON, (uint8_t)clientOf(c));
  }
  void onConnectFail(NimBLEClient* c, int reason) override {
    ev(EV_PEER_OFF, (uint8_t)clientOf(c));
  }
  void onDisconnect(NimBLEClient* c, int reason) override {
    ev(EV_PEER_OFF, (uint8_t)clientOf(c));
  }
};

class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    if (!d->isAdvertisingService(SVC)) {
      return;
    }
    uint8_t p[8 + 23];
    memcpy(p, d->getAddress().getVal(), 6);
    p[6] = d->getAddress().getType();
    p[7] = (uint8_t)d->getRSSI();
    std::string name = d->getName();
    size_t n = name.size() > 23 ? 23 : name.size();
    memcpy(p + 8, name.data(), n);
    ev(EV_FOUND, 0, 0, p, 8 + n);
  }
};

static ServerCB serverCB;
static CharCB charCB;
static ClientCB clientCB;
static ScanCB scanCB;

// ------------------------------------------------------------------------------------------- the list ---
static bool busy(int i) {  // a connection has it
  for (auto& k : cls) {
    if (k.st != IDLE && k.peer == i) {
      return true;
    }
  }
  return false;
}

static bool around(int i) {
  return peers[i].seenAt && millis() - peers[i].seenAt < AROUND;
}

BleState ble::state(int i) {
  for (auto& k : cls) {
    if (k.peer == i && k.st != IDLE && k.st != CLOSING) {
      return k.st == READY ? BLE_ON : BLE_CONNECTING;
    }
  }
  return BLE_OFF;
}

struct Saved {
  uint8_t addr[6], type;
  char name[24];
};

static void save() {
  Saved s[BLE_SAVED];
  int n = 0;
  for (int i = 0; i < count && n < BLE_SAVED; i++) {
    if (peers[i].saved) {
      memcpy(s[n].addr, peers[i].addr, 6);
      s[n].type = peers[i].type;
      strlcpy(s[n].name, peers[i].name, sizeof s[n].name);
      n++;
    }
  }
  prefs.putBytes("peers", s, n * sizeof(Saved));
}

static void load() {
  Saved s[BLE_SAVED];
  int n = prefs.getBytes("peers", s, sizeof s) / sizeof(Saved);
  for (int i = 0; i < n; i++) {
    BlePeer& p = peers[count++];
    memset(&p, 0, sizeof p);
    memcpy(p.addr, s[i].addr, 6);
    p.type = s[i].type;
    strlcpy(p.name, s[i].name, sizeof p.name);
    p.saved = true;
  }
}

static void found(const uint8_t* p, size_t n) {
  int i = -1;
  for (int j = 0; j < count && i < 0; j++) {
    if (!memcmp(peers[j].addr, p, 6) && peers[j].type == p[6]) {
      i = j;
    }
  }
  int8_t rssi = (int8_t)p[7];
  if (i < 0) {
    if (count < BLE_PEERS) {
      i = count++;
    } else {  // instead of one that's gone, or else the weakest (never one that's remembered or connected)
      for (int j = 0; j < count; j++) {
        if (peers[j].saved || busy(j)) {
          continue;
        }
        if (i < 0 || (around(i) && !around(j)) || (around(i) == around(j) && peers[j].rssi < peers[i].rssi)) {
          i = j;
        }
      }
      if (i < 0 || (around(i) && peers[i].rssi >= rssi)) {
        return;
      }
    }
    memset(&peers[i], 0, sizeof peers[i]);
    memcpy(peers[i].addr, p, 6);
    peers[i].type = p[6];
  }
  if (n > 8) {
    size_t k = n - 8 < sizeof peers[i].name - 1 ? n - 8 : sizeof peers[i].name - 1;
    memcpy(peers[i].name, p + 8, k);
    peers[i].name[k] = 0;
  } else if (!peers[i].name[0]) {
    const uint8_t* a = peers[i].addr;  // least significant byte first
    snprintf(peers[i].name, sizeof peers[i].name, "BLE %02X:%02X:%02X", a[2], a[1], a[0]);
  }
  peers[i].rssi = rssi;
  peers[i].seenAt = millis() | 1;
  dirty = true;
}

// --------------------------------------------------------------------------------------------- slots ---
static int freeSlot() {
  for (int s = 0; s < SLOTS; s++) {
    if (slots[s].kind == FREE) {
      return s;
    }
  }
  return -1;
}

static void take(int s, uint8_t kind, int8_t cl, uint16_t conn, const char* name) {
  memset(&slots[s], 0, sizeof slots[s]);
  slots[s].kind = kind;
  slots[s].cl = cl;
  slots[s].conn = conn;
  hub.mount(LINK_USB + s, name);
  dirty = true;
}

static void release(int s) {
  if (s >= 0 && s < SLOTS && slots[s].kind != FREE) {
    slots[s].kind = FREE;
    hub.umount(LINK_USB + s);
    dirty = true;
  }
}

// BLE MIDI packets to MIDI bytes: the header byte, then a timestamp byte before every status byte (and maybe
// before running status data)
static void decode(int s, const uint8_t* p, size_t n) {
  static uint8_t o[EV_DATA];
  size_t k = 0;
  if (n < 2 || !(p[0] & 0x80)) {
    return;
  }
  for (size_t i = 1; i < n; i++) {
    if (!(p[i] & 0x80)) {
      o[k++] = p[i];
    } else if (i + 1 < n && p[i + 1] & 0x80) {  // a timestamp, and the status after it
      o[k++] = p[++i];
    }
  }
  hub.play(LINK_USB + s, o, k);
}

// MIDI bytes to BLE MIDI packets, one message at a time (a SysEx goes on in the next packet)
static void flush(int s) {
  auto& o = slots[s];
  if (o.n > 1) {
    if (o.kind == HOST) {
      chr->notify(o.pkt, o.n, o.conn);
    } else if (o.kind == PEER && cls[o.cl].ch) {
      cls[o.cl].ch->writeValue(o.pkt, o.n, false);
    }
  }
  o.n = 0;
}

static void room(int s, uint8_t need) {
  auto& o = slots[s];
  if (o.n && o.n + need > o.max) {
    flush(s);
  }
  if (!o.n) {
    uint16_t mtu = o.kind == HOST ? server->getPeerMTU(o.conn) : cls[o.cl].c->getMTU();
    o.max = mtu > PKT + 3 ? PKT : mtu > 23 ? mtu - 3
                                           : 20;
    o.pkt[o.n++] = 0x80 | (millis() >> 7 & 0x3F);
  }
}

static void add(int s, uint8_t b) {
  slots[s].pkt[slots[s].n++] = b;
}

static void timestamp(int s) {
  add(s, 0x80 | (millis() & 0x7F));
}

static void message(int s, const uint8_t* m, uint8_t n) {
  room(s, 1 + n);
  timestamp(s);
  for (uint8_t i = 0; i < n; i++) {
    add(s, m[i]);
  }
}

static void put(int s, uint8_t b) {
  auto& o = slots[s];
  if (b >= 0xF8) {  // realtime: anywhere, even inside a SysEx
    message(s, &b, 1);
  } else if (b == 0xF0) {
    o.sx = true;
    o.st = 0;
    message(s, &b, 1);
  } else if (b == 0xF7) {
    if (o.sx) {
      message(s, &b, 1);
      o.sx = false;
    }
  } else if (b & 0x80) {
    o.sx = false;
    o.st = b;
    o.have = 0;
    o.need = b < 0xF0 ? map_len(b) - 1 : b == 0xF2            ? 2
                                     : b == 0xF1 || b == 0xF3 ? 1
                                                              : 0;
    if (!o.need) {
      if (b == 0xF6) {
        message(s, &b, 1);
      }
      o.st = 0;
    }
  } else if (o.sx) {
    room(s, 1);
    add(s, b);
  } else if (o.st) {
    o.msg[o.have++] = b;
    if (o.have == o.need) {
      uint8_t m[3] = {o.st, o.msg[0], o.msg[1]};
      message(s, m, 1 + o.need);
      o.have = 0;
      if (o.st >= 0xF0) {
        o.st = 0;
      }
    }
  }
}

void ble::send(uint8_t d, const uint8_t* p, uint32_t n) {
  int s = d - LINK_USB;
  if (s < 0 || s >= SLOTS || slots[s].kind == FREE || (slots[s].kind == PEER && !cls[slots[s].cl].ch)) {
    return;
  }
  for (uint32_t i = 0; i < n; i++) {
    put(s, p[i]);
  }
}

// ------------------------------------------------------------------------------------------ the loop ---
void ble::pump() {
  if (!evq) {
    return;
  }
  size_t n;
  uint8_t* b;
  for (int budget = 32; budget && (b = (uint8_t*)xRingbufferReceive(evq, &n, 0)); budget--) {
    Ev e;
    memcpy(&e, b, sizeof e);
    const uint8_t* p = b + sizeof e;
    n -= sizeof e;
    int s = -1;
    for (int k = 0; k < SLOTS && s < 0; k++) {
      if (slots[k].kind == HOST && slots[k].conn == e.conn) {
        s = k;
      }
    }
    if (e.type == EV_HOST_ON) {
      int f = freeSlot();
      if (f < 0) {
        server->disconnect(e.conn);
      } else {
        take(f, HOST, -1, e.conn, BLE_HOST_NAME);
      }
    } else if (e.type == EV_HOST_OFF) {
      release(s);
    } else if (e.type == EV_HOST_DATA && s >= 0) {
      decode(s, p, n);
    } else if (e.type == EV_FOUND && n >= 8) {
      found(p, n);
    } else if (e.who < BLE_SAVED) {
      auto& k = cls[e.who];
      if (e.type == EV_PEER_ON && k.st == CONNECTING) {
        k.st = SETUP;  // the main loop finds its MIDI
        dirty = true;
      } else if (e.type == EV_PEER_OFF) {
        release(k.slot);
        k.st = IDLE;
        k.peer = k.slot = -1;
        k.ch = 0;
        dirty = true;
      } else if (e.type == EV_PEER_DATA && k.st == READY) {
        decode(k.slot, p, n);
      }
    }
    vRingbufferReturnItem(evq, b);
  }
  for (int s = 0; s < SLOTS; s++) {
    if (slots[s].kind != FREE) {
      flush(s);
    }
  }
}

static void drop(int k) {
  if (cls[k].st == CONNECTING) {
    cls[k].c->cancelConnect();
  } else if (cls[k].st == SETUP || cls[k].st == READY) {
    cls[k].c->disconnect();
  } else {
    return;
  }
  cls[k].st = CLOSING;
  cls[k].at = millis();
}

// the MIDI of a keyboard or synth that just connected (blocks for a moment)
static void ready(int k) {
  auto& c = cls[k];
  NimBLERemoteService* sv = c.c->getService(SVC);
  NimBLERemoteCharacteristic* ch = sv ? sv->getCharacteristic(CHR) : 0;
  int s = freeSlot();
  uint8_t who = (uint8_t)k;
  if (!ch || s < 0 || c.peer < 0) {
    drop(k);
    return;
  }
  ch->readValue();  // as the spec asks: starts the pairing if it wants one
  if (!ch->canNotify() ||
      !ch->subscribe(true, [who](NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) { ev(EV_PEER_DATA, who, 0, d, n); })) {
    drop(k);
    return;
  }
  c.ch = ch;
  c.slot = (int8_t)s;
  c.st = READY;
  take(s, PEER, (int8_t)k, 0, ble::peers[c.peer].name);
}

static int connections() {
  int n = 0;
  for (auto& s : slots) {
    n += s.kind == HOST;
  }
  for (auto& k : cls) {
    n += k.st != IDLE;
  }
  return n;
}

static void dial(int i) {
  int k = -1;
  for (int j = 0; j < BLE_SAVED && k < 0; j++) {
    if (cls[j].st == IDLE) {
      k = j;
    }
  }
  if (k < 0 || connections() >= BLE_SAVED || freeSlot() < 0) {
    return;
  }
  auto& c = cls[k];
  if (!c.c) {
    c.c = NimBLEDevice::createClient();
    c.c->setClientCallbacks(&clientCB, false);
    c.c->setConnectTimeout(8000);
    c.c->setConnectionParams(6, 12, 0, 300);  // 7.5 to 15 ms: MIDI wants it quick
  }
  c.peer = (int8_t)i;
  c.slot = -1;
  c.st = CONNECTING;
  c.at = millis();
  if (!c.c->connect(NimBLEAddress(ble::peers[i].addr, ble::peers[i].type), true, true, true)) {
    c.st = IDLE;
    c.peer = -1;
    ble::peers[i].seenAt = 0;  // try again when it's found again
  }
  dirty = true;
}

void ble::loop() {
  if (!evq) {
    return;
  }
  for (int k = 0; k < BLE_SAVED; k++) {
    if (cls[k].st == SETUP) {
      ready(k);
    }
    if ((cls[k].st == CONNECTING || cls[k].st == CLOSING) && millis() - cls[k].at > 15000) {
      cls[k].c->cancelConnect();  // it never answered
      release(cls[k].slot);
      cls[k].st = IDLE;
      cls[k].peer = -1;
      dirty = true;
    }
  }
  if (!on) {
    return;
  }
  bool connecting = false, missing = false;
  for (auto& k : cls) {
    connecting |= k.st == CONNECTING || k.st == SETUP;
  }
  for (int i = 0; i < count; i++) {
    if (peers[i].saved && !busy(i)) {
      missing = true;
      if (!connecting && around(i)) {
        dial(i);
        connecting = true;
      }
    }
  }
  NimBLEScan* sc = NimBLEDevice::getScan();
  if (!connecting && !sc->isScanning() && connections() < BLE_SAVED) {
    if (searching) {
      sc->start(10000, false, false);  // in rounds: each one finds everything again
    } else if (missing && millis() - scanAt > 10000) {
      scanAt = millis();
      sc->start(3000, false, false);
    }
  }
}

// ----------------------------------------------------------------------------------------------- api ---
void ble::begin(bool want) {
  evq = xRingbufferCreate(8192, RINGBUF_TYPE_NOSPLIT);
  prefs.begin("fm1bt");
  load();
  for (auto& k : cls) {
    k.peer = k.slot = -1;
  }
  NimBLEDevice::init(BLE_NAME);
  NimBLEDevice::setSecurityAuth(true, false, true);  // bond if asked ("just works")
  server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCB, false);
  NimBLEService* sv = server->createService(SVC);
  chr = sv->createCharacteristic(CHR, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
  chr->setCallbacks(&charCB);
  sv->start();
  NimBLEAdvertisementData ad, sr;
  ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  ad.setCompleteServices(SVC);
  sr.setName(BLE_NAME);
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAdvertisementData(ad);
  adv->setScanResponseData(sr);
  adv->enableScanResponse(true);
  NimBLEScan* sc = NimBLEDevice::getScan();
  sc->setScanCallbacks(&scanCB, false);
  sc->setActiveScan(true);  // names are often in the scan response
  sc->setMaxResults(0);     // only the callback
  sc->setInterval(100);
  sc->setWindow(50);
  on = !want;
  setOn(want);
}

void ble::setOn(bool want) {
  if (!evq || want == on) {
    return;
  }
  on = want;
  server->advertiseOnDisconnect(on);
  if (on) {
    NimBLEDevice::getAdvertising()->start();
  } else {
    NimBLEDevice::getScan()->stop();
    NimBLEDevice::getAdvertising()->stop();
    for (auto& s : slots) {
      if (s.kind == HOST) {
        server->disconnect(s.conn);
      }
    }
    for (int k = 0; k < BLE_SAVED; k++) {
      drop(k);
    }
  }
  dirty = true;
}

bool ble::isOn() {
  return on;
}

void ble::search(bool want) {
  if (searching && !want && evq) {
    NimBLEDevice::getScan()->stop();
  }
  searching = want;
}

bool ble::host() {
  for (auto& s : slots) {
    if (s.kind == HOST) {
      return true;
    }
  }
  return false;
}

bool ble::shown(int i) {
  return i < count && (peers[i].saved || busy(i) || around(i));
}

void ble::toggle(int i) {
  if (i < 0 || i >= count) {
    return;
  }
  BlePeer& p = peers[i];
  if (p.saved) {
    p.saved = false;
    for (int k = 0; k < BLE_SAVED; k++) {
      if (cls[k].peer == i) {
        drop(k);
      }
    }
  } else {
    int n = 0;
    for (int j = 0; j < count; j++) {
      n += peers[j].saved;
    }
    if (n >= BLE_SAVED) {
      return;
    }
    p.saved = true;
    p.seenAt = millis() | 1;  // it's here: connect now
  }
  save();
  dirty = true;
}

bool ble::changed() {
  bool d = dirty;
  dirty = false;
  return d;
}

void ble::status(Print& out) {
  out.printf("bt %s, heap %u (largest %u)\n", on ? "on" : "off", (unsigned)ESP.getFreeHeap(),
             (unsigned)ESP.getMaxAllocHeap());
  for (int s = 0; s < SLOTS; s++) {
    if (slots[s].kind != FREE) {
      out.printf("dev %d: %s\n", LINK_USB + s, slots[s].kind == HOST ? BLE_HOST_NAME : peers[cls[slots[s].cl].peer].name);
    }
  }
  for (int i = 0; i < count; i++) {
    const uint8_t* a = peers[i].addr;
    out.printf("%d %s %02x:%02x:%02x:%02x:%02x:%02x %d dBm%s%s state %d\n", i, peers[i].name, a[5], a[4], a[3], a[2],
               a[1], a[0], peers[i].rssi, peers[i].saved ? " saved" : "", around(i) ? " around" : "", state(i));
  }
}
