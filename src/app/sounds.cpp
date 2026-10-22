#include "sounds.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <thread>

#include "catalog.h"
#include "fm1.h"
#include "hal.h"
#include "midi.h"

static void wait(uint32_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// each complete message: its start and length
static std::vector<std::pair<uint32_t, uint32_t>> messages(const std::vector<uint8_t>& d, uint32_t* outside = 0) {
  std::vector<std::pair<uint32_t, uint32_t>> out;
  uint32_t start = 0, loose = 0;
  bool in = false;
  for (uint32_t i = 0; i < d.size(); i++) {
    if (d[i] == 0xF0) {
      in = true;
      start = i;
    } else if (d[i] == 0xF7 && in) {
      out.push_back({start, i + 1 - start});
      in = false;
    } else if (!in) {
      loose++;
    }
  }
  if (outside) {
    *outside = loose + (in ? 1 : 0);
  }
  return out;
}

// ------------------------------------------------------------------------------------------- DX7 ---
#define DX7_VOICE 155   // a single voice (VCED)
#define DX7_BANK 4096   // 32 packed voices (VMEM)
#define DX7_PACKED 128  // one of them

static std::string dx7Name(const uint8_t* p) {
  char n[11];
  for (int i = 0; i < 10; i++) {
    n[i] = p[i] >= 32 && p[i] < 127 ? (char)p[i] : ' ';
  }
  n[10] = 0;
  std::string s = n;
  s.erase(s.find_last_not_of(' ') + 1);
  return s;
}

static bool dx7Sum(const uint8_t* p, uint32_t n, uint8_t sum) {
  uint32_t s = 0;
  for (uint32_t i = 0; i < n; i++) {
    s += p[i];
  }
  return ((-s) & 0x7F) == sum;
}

// F0 43 0n 09 20 00, 4096 bytes, the sum, F7
static bool isBank(const uint8_t* m, uint32_t n) {
  return n == DX7_BANK + 8 && m[1] == 0x43 && !(m[2] & 0xF0) && m[3] == 0x09 && m[4] == 0x20 && m[5] == 0;
}

// F0 43 0n 00 01 1B, 155 bytes, the sum, F7
static bool isVoice(const uint8_t* m, uint32_t n) {
  return n == DX7_VOICE + 8 && m[1] == 0x43 && !(m[2] & 0xF0) && m[3] == 0 && m[4] == 0x01 && m[5] == 0x1B;
}

sounds::Info sounds::describe(const std::vector<uint8_t>& data) {
  Info info;
  uint32_t outside = 0;
  auto ms = messages(data, &outside);
  info.messages = ms.size();
  info.good = !outside && !ms.empty();
  int banks = 0, voices = 0;
  for (auto& [at, n] : ms) {
    const uint8_t* m = data.data() + at;
    if (isBank(m, n)) {
      banks++;
      info.good &= dx7Sum(m + 6, DX7_BANK, m[6 + DX7_BANK]);
      for (int v = 0; v < 32; v++) {
        info.names.push_back(dx7Name(m + 6 + v * DX7_PACKED + 118));
      }
    } else if (isVoice(m, n)) {
      voices++;
      info.good &= dx7Sum(m + 6, DX7_VOICE, m[6 + DX7_VOICE]);
      info.names.push_back(dx7Name(m + 6 + 145));
    }
  }
  char s[48];
  if (ms.empty()) {
    snprintf(s, sizeof s, "No SysEx in it");
  } else if (banks + voices < (int)ms.size()) {
    snprintf(s, sizeof s, "%d SysEx message%s", (int)ms.size(), ms.size() == 1 ? "" : "s");
  } else if (banks == 1 && !voices) {
    snprintf(s, sizeof s, "DX7 bank: 32 voices");
  } else if (voices == 1 && !banks) {
    snprintf(s, sizeof s, "DX7 voice");
  } else {
    snprintf(s, sizeof s, "DX7: %d voices", (int)info.names.size());
  }
  info.kind = s;
  return info;
}

// --------------------------------------------------------------------------------------------- files ---
std::vector<sounds::File> sounds::list() {
  std::vector<File> out;
  std::string dir = catalog::path(SYX_DIR);
  DIR* d = dir.empty() ? 0 : opendir(dir.c_str());
  if (!d) {
    return out;
  }
  while (dirent* e = readdir(d)) {
    size_t n = strlen(e->d_name);
    if (e->d_name[0] == '.' || n < 5 || strcasecmp(e->d_name + n - 4, ".syx")) {
      continue;
    }
    struct stat st;
    if (!stat((dir + "/" + e->d_name).c_str(), &st)) {
      out.push_back({e->d_name, (uint32_t)st.st_size});
    }
  }
  closedir(d);
  std::sort(out.begin(), out.end(), [](const File& a, const File& b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
  return out;
}

bool sounds::load(const std::string& name, std::vector<uint8_t>& out) {
  out.clear();
  FILE* f = fopen(catalog::path(std::string(SYX_DIR "/") + name).c_str(), "rb");
  if (!f) {
    return false;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  bool ok = n >= 0 && n <= 1024 * 1024;
  if (ok) {
    out.resize(n);
    ok = fread(out.data(), 1, n, f) == (size_t)n;
  }
  fclose(f);
  return ok;
}

std::string sounds::save(const std::vector<uint8_t>& data, const char* prefix) {
  std::string dir = catalog::path(SYX_DIR);
  if (dir.empty()) {
    return "";
  }
  mkdir(dir.c_str(), 0777);
  struct stat st;
  for (int n = 1; n < 1000; n++) {
    std::string name = std::string(prefix) + "-" + std::to_string(n) + ".syx", p = dir + "/" + name;
    if (!stat(p.c_str(), &st)) {
      continue;
    }
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) {
      return "";
    }
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok &= !fclose(f);
    if (!ok) {
      remove(p.c_str());
    }
    return ok ? name : "";
  }
  return "";
}

// ------------------------------------------------------------------------------------------- talking ---
// sends msg to device d, then waits up to ms for a message from it that match() takes; the matching one in reply
template <typename F>
static bool ask(int d, const uint8_t* msg, uint32_t n, uint32_t ms, F match, std::vector<uint8_t>& reply) {
  midi::capture(d, true);
  bool got = false;
  if (midi::sendWait(d, msg, n, 500)) {
    std::vector<uint8_t> all;
    for (uint32_t start = hal::millis(); !got && hal::millis() - start < ms; wait(5)) {
      midi::captured(d, all);
      for (auto& [at, len] : messages(all)) {
        if (match(all.data() + at, len)) {
          reply.assign(all.begin() + at, all.begin() + at + len);
          got = true;
          break;
        }
      }
    }
  }
  midi::capture(d, false);
  return got;
}

#define READ 0x23
#define READ_VOICE 5

const char* sounds::readVoice(int d, std::vector<uint8_t>& syx) {
  static const uint8_t REQ[] = {READ_VOICE, 0, 0, 0, 0, DX7_VOICE, 0, 0};  // what, where (4), how much (3)
  uint8_t msg[32], payload[8 + DX7_VOICE + 16];
  uint32_t n = fm1::frame(READ, REQ, sizeof REQ, msg);
  std::vector<uint8_t> reply;
  int len = -1;
  bool got = ask(
      d, msg, n, 1000,
      [&](const uint8_t* m, uint32_t k) {
        len = fm1::unframe(m, k, READ, payload, sizeof payload);
        return len >= 8 + DX7_VOICE;
      },
      reply);
  if (!got) {
    return "It didn't send its voice.";
  }
  // a DX7 single voice: F0 43 00 00 01 1B, the 155 bytes, the sum, F7
  syx = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};
  uint32_t sum = 0;
  for (int i = 0; i < DX7_VOICE; i++) {
    uint8_t b = payload[8 + i] & 0x7F;
    sum += b;
    syx.push_back(b);
  }
  syx.push_back((-sum) & 0x7F);
  syx.push_back(0xF7);
  return 0;
}

void sounds::probe(int d, Speaks& out) {
  memset(&out, 0, sizeof out);
  if (!fm1::candidate(d)) {
    return;  // never other gear
  }
  std::vector<uint8_t> reply;
  out.stockVoice = !readVoice(d, reply);

  static const uint8_t INFO[] = {0xF0, 0x7D, 0x46, 0x4C, 0x01, 0xF7};
  if (ask(d, INFO, sizeof INFO, 800, [](const uint8_t* m, uint32_t n) { return n > 6 && !memcmp(m, INFO, 5); }, reply)) {
    out.felucca = true;
    uint32_t k = 0;
    for (uint32_t i = 5; i < reply.size() - 1 && reply[i] && k < sizeof out.version - 1; i++) {
      out.version[k++] = (char)reply[i];
    }
    out.version[k] = 0;
  }

  // its command 0 (connect), token 1: answered with command 0 | 64 and the same token
  uint8_t open[] = {0xF0, 0x7D, 0x46, 0x4D, 0x02, 0x00, 0x01, 0x00, 0, 0xF7};
  uint32_t sum = 0;
  for (int i = 1; i < 8; i++) {
    sum += open[i];
  }
  open[8] = (-sum) & 0x7F;
  out.open = ask(
      d, open, sizeof open, 800,
      [](const uint8_t* m, uint32_t n) { return n >= 10 && !memcmp(m, "\xF0\x7D\x46\x4D\x02\x40\x01\x00", 8); }, reply);
  hal::log("sounds: %d: stock voice %d, felucca %d (%s), open %d", d, out.stockVoice, out.felucca, out.version, out.open);
}

// the update command: never sent from a file (it would leave the FM-1 in update mode)
static const uint8_t UPGRADE[] = {0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7};

const char* sounds::send(int d, const std::vector<uint8_t>& data, void (*progress)(uint32_t, uint32_t)) {
  auto ms = messages(data);
  if (ms.empty()) {
    return "There's no SysEx in it.";
  }
  for (auto& [at, n] : ms) {
    if (n == sizeof UPGRADE && !memcmp(data.data() + at, UPGRADE, n)) {
      return "It has the FM-1's update command in it: not sent.";
    }
  }
  midi::thru = false;  // nothing else in the middle of a message
  const char* err = 0;
  uint32_t done = 0;
  for (auto& [at, n] : ms) {
    for (uint32_t i = 0; i < n && !err; i += 256) {
      if (!midi::sendWait(d, data.data() + at + i, std::min<uint32_t>(256, n - i), 2000)) {
        err = "It stopped taking MIDI (or went).";
      }
    }
    if (err) {
      break;
    }
    done += n;
    if (progress) {
      progress(done, data.size());
    }
    wait(150);  // a moment to take each in
  }
  midi::thru = true;
  return err;
}
