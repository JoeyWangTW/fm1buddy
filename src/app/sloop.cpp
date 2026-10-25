#include "sloop.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include <algorithm>
#include <chrono>
#include <thread>

#include "catalog.h"
#include "hal.h"
#include "midi.h"
#include "sloopfmt.h"

using namespace sloopfmt;

enum { INFO = 1,
       SMP_BEGIN = 11,
       SMP_WRITE = 12,
       SMP_END = 13,
       SMP_ERASE = 14,
       SMP_INFO = 15,
       BK_LIST = 34,
       BK_GET = 35,
       BK_PUT = 36 };

static const int RESTORE_ORDER[] = {6, 7, 8, 9, 10, 11, 2, 3, 4, 5, 0, 1};  // banks, projects, working, settings
static const int SAMPLES[] = {32, 33, 34, 35};

// ------------------------------------------------------------------------------------------- talking ---
// cmd with args to device d; its reply's args (after the cmd, before F7) into out. Up to tries times, ms each. Pushes
// (cmds 23, 24, 26, 32) are never replies: matching by cmd leaves them out
static bool request(int d, uint8_t cmd, const std::vector<uint8_t>& args, std::vector<uint8_t>& out, uint32_t ms = 1500,
                    int tries = 2) {
  std::vector<uint8_t> msg = {0xF0, 0x7D, 0x46, 0x4C, cmd};
  msg.insert(msg.end(), args.begin(), args.end());
  msg.push_back(0xF7);
  for (int t = 0; t < tries; t++) {
    midi::capture(d, true);
    bool got = false;
    if (midi::sendWait(d, msg.data(), (uint32_t)msg.size(), 500)) {
      std::vector<uint8_t> all;
      for (uint32_t start = hal::millis(); !got && hal::millis() - start < ms;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        midi::captured(d, all);
        for (size_t i = 0; i + 5 < all.size() && !got; i++) {
          if (all[i] != 0xF0 || all[i + 1] != 0x7D || all[i + 2] != 0x46 || all[i + 3] != 0x4C || all[i + 4] != cmd) {
            continue;
          }
          size_t end = i + 5;
          while (end < all.size() && all[end] != 0xF7) {
            end++;
          }
          if (end < all.size()) {
            out.assign(all.begin() + i + 5, all.begin() + end);
            got = true;
          }
        }
      }
    }
    midi::capture(d, false);
    if (got) {
      return true;
    }
  }
  return false;
}

static const char* rcText(int rc) {
  switch (rc) {
    case 2:
      return "The FM-1 says that isn't a valid SLOOP object.";
    case 3:
      return "Stop the song on the FM-1 first.";
    case 4:
      return "The FM-1's flash didn't take it.";
    case 5:
      return "The FM-1 lost track of it: try again.";
  }
  return "The FM-1 didn't take it.";
}

// ---------------------------------------------------------------------------------------------- INFO ---
// has s a word in it (any case)?
static bool has(const std::string& s, const char* word) {
  std::string up;
  for (char c : s) {
    up += (char)toupper((unsigned char)c);
  }
  return up.find(word) != std::string::npos;
}

bool sloop::Info::sloop() const {
  return has(version, "SLOOP");
}

bool sloop::parseInfo(const uint8_t* a, uint32_t n, Info& out) {
  out = Info();
  uint32_t i = 0;
  auto str = [&](std::string* s) {
    while (i < n && a[i]) {
      if (s) {
        *s += (char)a[i];
      }
      i++;
    }
    i++;  // (its 0)
  };
  str(&out.version);
  if (i + 5 > n) {
    return !out.version.empty();
  }
  int nengines = a[i];
  out.pcount = a[i + 1];
  i += 5;  // NENGINES, P_COUNT, G_COUNT, NSTEP, P_E0
  for (int e = 0; e < nengines && i < n; e++) {
    str(0);
  }
  if (i < n) {
    i++;  // NTRK
  }
  out.proto = i < n ? a[i] : 0;
  return true;
}

const char* sloop::info(int d, Info& out) {
  std::vector<uint8_t> r;
  if (!request(d, INFO, {}, r, 1000, 2) || !parseInfo(r.data(), (uint32_t)r.size(), out)) {
    return "The FM-1 didn't answer (is it running SLOOP?).";
  }
  return 0;
}

const char* sloop::slots(int d, std::vector<Slot>& out) {
  out.clear();
  std::vector<uint8_t> r;
  if (!request(d, SMP_INFO, {}, r) || r.size() < 2) {
    return "The FM-1 didn't say what's in its sample slots.";
  }
  size_t i = 2;
  for (int k = 0; k < r[0] && i < r.size(); k++) {
    Slot s;
    s.zones = r[i++];
    while (i < r.size() && r[i]) {
      s.name += (char)r[i++];
    }
    i++;
    s.kib = i < r.size() ? r[i++] : 0;
    out.push_back(s);
  }
  return 0;
}

// --------------------------------------------------------------------------------------------- files ---
const sloop::Object* sloop::Backup::get(int id) const {
  for (const Object& o : objects) {
    if (o.id == id) {
      return &o;
    }
  }
  return 0;
}

std::string sloop::objectName(int id) {
  static const char* NAMES[] = {"working project", "settings", "project A", "project B", "project C", "project D",
                                "presets 1-16", "presets 17-32", "FM6 patches", "drum kits", "MIDI map", "theme"};
  if (id >= 0 && id < 12) {
    return NAMES[id];
  }
  if (id >= 32 && id < 36) {
    return "sample slot USR" + std::to_string(id - 31);
  }
  return "object " + std::to_string(id);
}

static std::vector<std::string> listDir(const std::string& dir, bool (*want)(const std::string&, bool)) {
  std::vector<std::string> out;
  if (DIR* d = dir.empty() ? 0 : opendir(dir.c_str())) {
    while (dirent* e = readdir(d)) {
      std::string n = e->d_name;
      struct stat st;
      if (n[0] != '.' && !stat((dir + "/" + n).c_str(), &st) && want(n, S_ISDIR(st.st_mode))) {
        out.push_back(n);
      }
    }
    closedir(d);
  }
  return out;
}

static bool ends(const std::string& s, const char* e) {
  size_t k = strlen(e);
  return s.size() > k && !strcasecmp(s.c_str() + s.size() - k, e);
}

std::vector<std::string> sloop::backups() {
  auto v = listDir(catalog::path(SLOOP_DIR), [](const std::string& n, bool dir) { return !dir && ends(n, ".json"); });
  // the newest first: by the number in the name, then the name
  auto num = [](const std::string& s) {
    size_t i = s.find_first_of("0123456789");
    return i == std::string::npos ? 0L : atol(s.c_str() + i);
  };
  std::sort(v.begin(), v.end(), [&](const std::string& a, const std::string& b) {
    return num(a) != num(b) ? num(a) > num(b) : a > b;
  });
  return v;
}

std::vector<std::string> sloop::sampleSources() {
  auto v = listDir(catalog::path(SLOOP_SAMPLES_DIR), [](const std::string& n, bool dir) { return dir || ends(n, ".wav"); });
  std::sort(v.begin(), v.end());
  return v;
}

static bool loadFile(const std::string& path, std::vector<char>& out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) {
    return false;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize(n > 0 ? (size_t)n + 1 : 1);
  bool ok = n >= 0 && fread(out.data(), 1, (size_t)n, f) == (size_t)n;
  out[out.size() - 1] = 0;
  fclose(f);
  return ok;
}

bool sloop::read(const std::string& name, Backup& out, std::string& why) {
  out = Backup();
  std::vector<char> text;
  if (!loadFile(catalog::path(std::string(SLOOP_DIR "/") + name), text)) {
    why = "Can't read it from the SD card.";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, text.data())) {  // (in place: its strings stay in text)
    why = "It isn't a SLOOP backup (not JSON).";
    return false;
  }
  if (strcmp(doc["format"] | "", "sloop-backup") || (doc["version"] | 0) < 1 || !doc["objects"].is<JsonArray>()) {
    why = "It isn't a SLOOP backup.";
    return false;
  }
  out.firmware = doc["firmware"] | "";
  out.date = doc["date"] | "";
  for (JsonObject o : doc["objects"].as<JsonArray>()) {
    Object obj;
    obj.id = o["id"] | -1;
    uint32_t len = o["len"] | 0u, crc = o["crc"] | 0u;
    bool known = (obj.id >= 0 && obj.id < 12) || (obj.id >= 32 && obj.id < 36);
    if (!known || !unbase64(o["data"] | "", obj.data) || obj.data.size() != len ||
        crc32(obj.data.data(), obj.data.size()) != crc) {
      why = "It's damaged: " + objectName(obj.id) + " doesn't match its CRC.";
      return false;
    }
    out.objects.push_back(std::move(obj));
  }
  if (!out.get(0) || !out.get(1)) {
    why = "It's incomplete (no working project or settings).";
    return false;
  }
  return true;
}

std::string sloop::summary(const Backup& b) {
  std::string projects, s;
  int samples = 0;
  for (const Object& o : b.objects) {
    if (o.id >= 2 && o.id <= 5 && !o.data.empty()) {
      projects += std::string(projects.empty() ? "" : " ") + (char)('A' + o.id - 2);
    }
    samples += o.id >= 32 && !o.data.empty();
  }
  s = projects.empty() ? "no projects" : "projects " + projects;
  if (b.get(6) || b.get(7)) {
    s += ", presets";
  }
  if (samples) {
    s += ", " + std::to_string(samples) + " sample slot" + (samples == 1 ? "" : "s");
  }
  return s;
}

static bool writeBackup(const sloop::Backup& b, const char* prefix, std::string& saved) {
  std::string dir = catalog::path(SLOOP_DIR);
  if (dir.empty()) {
    return false;
  }
  mkdir(dir.c_str(), 0777);
  struct stat st;
  int n = 1;
  while (n < 10000 && !stat((dir + "/" + prefix + "-" + std::to_string(n) + ".json").c_str(), &st)) {
    n++;
  }
  saved = std::string(prefix) + "-" + std::to_string(n) + ".json";
  FILE* f = fopen((dir + "/" + saved).c_str(), "wb");
  if (!f) {
    return false;
  }
  // written out by hand: the data is big, and ArduinoJson would hold all of it twice
  JsonDocument head;
  head["firmware"] = b.firmware;
  head["date"] = b.date;
  std::string firmware, date;
  serializeJson(head["firmware"], firmware);
  serializeJson(head["date"], date);
  bool ok = fprintf(f, "{\"format\": \"sloop-backup\", \"version\": 1, \"firmware\": %s, \"date\": %s, \"objects\": [",
                    firmware.c_str(), date.c_str()) > 0;
  for (size_t i = 0; i < b.objects.size() && ok; i++) {
    const sloop::Object& o = b.objects[i];
    std::string data = base64(o.data.data(), o.data.size());
    ok = fprintf(f, "%s\n  {\"id\": %d, \"len\": %u, \"crc\": %u, \"data\": \"%s\"}", i ? "," : "", o.id,
                 (unsigned)o.data.size(), (unsigned)crc32(o.data.data(), o.data.size()), data.c_str()) > 0;
  }
  ok = ok && fprintf(f, "\n]}\n") > 0;
  ok = !fclose(f) && ok;
  if (!ok) {
    remove((dir + "/" + saved).c_str());
  }
  return ok;
}

// --------------------------------------------------------------------------------------------- backup ---
const char* sloop::backup(int d, const Info& info, std::string& saved, Progress p, const char* prefix) {
  if (info.proto < 6) {
    return "Backups need SLOOP 2.3 or later.";
  }
  std::vector<uint8_t> r;
  if (!request(d, BK_LIST, {}, r, 5000, 1) || r.size() < 2) {
    return "The FM-1 didn't list what it has.";
  }
  if (r[0]) {
    return rcText(r[0]);
  }
  struct Item {
    int id;
    uint32_t len, crc;
  };
  std::vector<Item> items;
  uint32_t total = 0;
  for (size_t i = 2, k = 0; k < r[1] && i + 11 <= r.size(); k++, i += 11) {
    items.push_back({r[i], u35(&r[i + 1]), u35(&r[i + 6])});
    total += items.back().len;
  }
  Backup b;
  b.firmware = info.version;
  time_t now = time(0);
  if (now > 1600000000) {  // (only with the clock set: the board has none until something sets it)
    char iso[32];
    strftime(iso, sizeof iso, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    b.date = iso;
  }
  uint32_t done = 0;
  for (const Item& it : items) {
    Object o;
    o.id = it.id;
    for (uint32_t off = 0; off < it.len; off += 256) {
      uint32_t n = it.len - off < 256 ? it.len - off : 256;
      std::vector<uint8_t> args = {(uint8_t)it.id};
      u35(off, args);
      args.push_back((uint8_t)(n & 0x7F));
      args.push_back((uint8_t)(n >> 7));
      if (!request(d, BK_GET, args, r, 1500, 3) || r.size() < 9 || r[0] != it.id) {
        return "The FM-1 stopped answering: keep it plugged in and try again.";
      }
      if (r[1]) {
        return r[1] == 5 ? "The FM-1 changed while it was read: try again." : "The FM-1 didn't give it.";
      }
      std::vector<uint8_t> data = unpack7(r.data() + 9, r.size() - 9);
      if (data.size() < n) {
        return "The FM-1 gave less than it said.";
      }
      o.data.insert(o.data.end(), data.begin(), data.begin() + n);
      done += n;
      if (p) {
        p(done, total);
      }
    }
    if (crc32(o.data.data(), o.data.size()) != it.crc) {
      return "What came doesn't match its CRC: try again.";
    }
    b.objects.push_back(std::move(o));
  }
  if (!writeBackup(b, prefix, saved)) {
    return "Can't write it to the SD card.";
  }
  hal::log("sloop: %s: %u objects, %u bytes", saved.c_str(), (unsigned)b.objects.size(), (unsigned)total);
  return 0;
}

// ---------------------------------------------------------------------------------------- writing it back ---
// the project format of an object ("FUN6": 6), 0 if it isn't a project
static int projFormat(const std::vector<uint8_t>& d) {
  return d.size() >= 4 && d[1] == 'N' && d[2] == 'U' && d[3] == 'F' ? d[0] - '0' : 0;
}

// the newest project format a device reads, from its parameter count
static int devFormat(const sloop::Info& info) {
  return info.pcount >= 71 ? 6 : info.pcount >= 61 ? 5
                                                   : 4;
}

// one object back with BK_PUT (begin, data, commit); skipped is set if the device doesn't have that kind of object
static const char* put(int d, int id, const std::vector<uint8_t>& data, uint32_t& done, uint32_t total,
                       sloop::Progress p, bool* skipped = 0) {
  std::vector<uint8_t> args = {0, (uint8_t)id}, r;
  u35((uint32_t)data.size(), args);
  u35(crc32(data.data(), data.size()), args);
  if (!request(d, BK_PUT, args, r, 1500, 2) || r.size() < 3) {
    return "The FM-1 stopped answering.";
  }
  if ((r[2] == 1 || r[2] == 5) && id >= 8 && id <= 11 && skipped) {
    *skipped = true;  // an older SLOOP without it (FM6 bank, drum kits, MIDI map, theme)
    return 0;
  }
  if (r[2]) {
    return rcText(r[2]);
  }
  for (size_t off = 0; off < data.size(); off += 256) {
    size_t n = data.size() - off < 256 ? data.size() - off : 256;
    args = {1, (uint8_t)id};
    u35((uint32_t)off, args);
    pack7(data.data() + off, n, args);
    if (!request(d, BK_PUT, args, r, 1500, 2) || r.size() < 3) {
      return "The FM-1 stopped answering.";
    }
    if (r[2]) {
      return rcText(r[2]);
    }
    done += (uint32_t)n;
    if (p) {
      p(done, total);
    }
  }
  if (!request(d, BK_PUT, {2, (uint8_t)id}, r, 5000, 1) || r.size() < 3) {
    return "The FM-1 stopped answering.";
  }
  return r[2] ? rcText(r[2]) : 0;
}

// a sample slot back: its header (the first 480 bytes) and data (from 512); empty: erased
static const char* putSlot(int d, int k, const std::vector<uint8_t>& obj, uint32_t& done, uint32_t total,
                           sloop::Progress p) {
  std::vector<uint8_t> r;
  if (obj.empty()) {
    if (!request(d, SMP_ERASE, {(uint8_t)k}, r, 2500, 1) || r.size() < 2) {
      return "The FM-1 stopped answering.";
    }
    return r[1] ? "The FM-1 didn't erase the sample slot." : 0;
  }
  if (obj.size() < SLOT_DATA_OFF) {
    return "A sample slot in it is too short.";
  }
  if (!request(d, SMP_BEGIN, {(uint8_t)k}, r, 1000, 1) || r.size() < 2) {
    return "The FM-1 stopped answering.";
  }
  if (r[1]) {
    return "The FM-1 didn't start the sample slot.";
  }
  for (size_t off = SLOT_DATA_OFF; off < obj.size(); off += 256) {
    size_t n = obj.size() - off < 256 ? obj.size() - off : 256;
    std::vector<uint8_t> args = {(uint8_t)k, (uint8_t)(off & 0x7F), (uint8_t)(off >> 7 & 0x7F), (uint8_t)(off >> 14 & 0x7F)};
    pack7(obj.data() + off, n, args);
    if (!request(d, SMP_WRITE, args, r, 1000, 2) || r.size() < 5) {
      return "The FM-1 stopped answering.";
    }
    if (r[4]) {
      return "The FM-1 didn't write the sample slot.";
    }
    done += (uint32_t)n;
    if (p) {
      p(done, total);
    }
  }
  std::vector<uint8_t> args = {(uint8_t)k};
  pack7(obj.data(), SLOT_HDR_LEN, args);
  if (!request(d, SMP_END, args, r, 2000, 1) || r.size() < 2) {
    return "The FM-1 stopped answering.";
  }
  static const char* END[] = {0, "its size", "its header", "its data's CRC", "the flash", "its zones"};
  if (r[1]) {
    static std::string why;
    why = std::string("The FM-1 didn't take the sample slot (") + (r[1] < 6 ? END[r[1]] : "?") + ").";
    return why.c_str();
  }
  return 0;
}

// is the backup's newest project one the FM-1 reads?
static const char* newer(const sloop::Backup& b, const sloop::Info& info) {
  int newest = 0;
  for (int id : {0, 2, 3, 4, 5}) {
    if (const sloop::Object* o = b.get(id)) {
      newest = std::max(newest, projFormat(o->data));
    }
  }
  if (newest > devFormat(info)) {
    static std::string why;
    why = "It's from a newer SLOOP (" + b.firmware + ") than the FM-1 has (" + info.version +
          "): update its SLOOP first (Firmware).";
    return why.c_str();
  }
  return 0;
}

const char* sloop::restore(int d, const std::string& name, std::string& savedFirst, Progress p) {
  Backup b;
  std::string why;
  if (!read(name, b, why)) {
    static std::string w;
    w = why;
    return w.c_str();
  }
  Info info;
  if (const char* e = sloop::info(d, info)) {
    return e;
  }
  if (const char* e = newer(b, info)) {
    return e;
  }
  // (2.6) a backup of a 2.5.1+ FM-1 without its MIDI map had the default one: restored as such
  if (!b.get(10) && strstr(b.firmware.c_str(), "SLOOP")) {
    int maj = 0, min = 0, pat = 0;
    const char* v = strstr(b.firmware.c_str(), "SLOOP") + 5;
    if (sscanf(v, " %d.%d.%d", &maj, &min, &pat) >= 2 && (maj > 2 || (maj == 2 && (min > 5 || (min == 5 && pat >= 1))))) {
      Object o{10, std::vector<uint8_t>(40, 0)};
      uint8_t def[] = {1, 2, 3, 10, 2};
      memcpy(&o.data[32], def, sizeof def);
      b.objects.push_back(o);
    }
  }
  // what it has now, first: to go back to
  if (const char* e = backup(d, info, savedFirst, 0, "before-restore")) {
    return e;
  }
  uint32_t total = 0, done = 0;
  for (const Object& o : b.objects) {
    total += (uint32_t)o.data.size();
  }
  for (int id : RESTORE_ORDER) {
    if (const Object* o = b.get(id)) {
      bool skipped = false;
      if (const char* e = put(d, id, o->data, done, total, p, &skipped)) {
        return e;
      }
      if (skipped) {
        hal::log("sloop: %s: the FM-1 has none (older SLOOP): skipped", objectName(id).c_str());
      }
    }
  }
  for (int id : SAMPLES) {
    if (const Object* o = b.get(id)) {
      if (const char* e = putSlot(d, id - 32, o->data, done, total, p)) {
        return e;
      }
    }
  }
  hal::log("sloop: %s restored", name.c_str());
  return 0;
}

const char* sloop::loadProject(int d, const std::string& name, int from, int to, Progress p) {
  Backup b;
  std::string why;
  static std::string w;
  if (!read(name, b, why)) {
    w = why;
    return w.c_str();
  }
  const Object* o = b.get(from);
  if (!o || o->data.empty() || !projFormat(o->data)) {
    return "That project isn't in it.";
  }
  Info info;
  if (const char* e = sloop::info(d, info)) {
    return e;
  }
  if (projFormat(o->data) > devFormat(info)) {
    w = "That project is from a newer SLOOP (" + b.firmware + "): update the FM-1's SLOOP first (Firmware).";
    return w.c_str();
  }
  uint32_t done = 0;
  return put(d, to, o->data, done, (uint32_t)o->data.size(), p);
}

// ---------------------------------------------------------------------------------------------- samples ---
const char* sloop::loadSamples(int d, const std::string& source, int slot, Progress p) {
  static std::string w;
  std::string path = catalog::path(std::string(SLOOP_SAMPLES_DIR "/") + source);
  std::vector<std::string> files;
  if (ends(source, ".wav")) {
    files.push_back(path);
  } else {
    for (const std::string& f : listDir(path, [](const std::string& n, bool dir) { return !dir && ends(n, ".wav"); })) {
      files.push_back(path + "/" + f);
    }
    std::sort(files.begin(), files.end());
  }
  if (files.empty()) {
    return "No .wav files in it.";
  }
  std::vector<Zone> zones;
  for (const std::string& f : files) {
    std::vector<char> raw;
    uint32_t rate;
    std::vector<double> x;
    std::string why;
    std::string base = f.substr(f.rfind('/') + 1);
    if (!loadFile(f, raw) || !readWav((const uint8_t*)raw.data(), raw.size() - 1, rate, x, why) || x.empty()) {
      w = base + ": " + (why.empty() ? "no audio" : why);
      return w.c_str();
    }
    zones.push_back({toInt16(resample(x, rate, SLOT_RATE)), noteFromName(base.substr(0, base.size() - 4))});
  }
  std::string name = ends(source, ".wav") ? source.substr(0, source.size() - 4) : source;
  std::vector<uint8_t> hdr, data;
  std::string why;
  if (!userSlot(name, zones, hdr, data, why)) {
    w = why;
    return w.c_str();
  }
  std::vector<uint8_t> obj(SLOT_DATA_OFF, 0);
  memcpy(obj.data(), hdr.data(), hdr.size());
  obj.insert(obj.end(), data.begin(), data.end());
  uint32_t done = 0;
  const char* e = putSlot(d, slot, obj, done, (uint32_t)data.size(), p);
  if (!e) {
    hal::log("sloop: %s into USR%d: %u zones, %u bytes", source.c_str(), slot + 1, (unsigned)zones.size(),
             (unsigned)data.size());
  }
  return e;
}
