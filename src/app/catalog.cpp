#include "catalog.h"

#include <ArduinoJson.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>

#include "hal.h"

std::vector<Firmware> catalog::hub, catalog::sd;
std::string catalog::hubUpdated;

std::string catalog::path(const std::string& p) {
  const char* root = hal::sd();
  return root ? root + p : "";
}

static bool readAll(const std::string& p, std::string& out) {
  FILE* f = fopen(p.c_str(), "rb");
  if (!f) {
    return false;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize(n > 0 ? n : 0);
  bool ok = n > 0 && fread(&out[0], 1, n, f) == (size_t)n;
  fclose(f);
  return ok;
}

static std::string baseName(const std::string& p) {
  size_t s = p.rfind('/');
  return s == std::string::npos ? p : p.substr(s + 1);
}

// the screen's fonts are ASCII: accented letters lose their accents, curly quotes and dashes straighten
static std::string ascii(const char* s) {
  static const char* LATIN =  // U+00C0 to U+017F
      "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty"
      "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiJjJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
  std::string o;
  const uint8_t* p = (const uint8_t*)s;
  while (*p) {
    uint32_t c = *p;
    int n = c < 0x80 ? 0 : c >= 0xF0 ? 3
                       : c >= 0xE0   ? 2
                       : c >= 0xC0   ? 1
                                     : 0;
    c &= n == 3 ? 0x07 : n == 2 ? 0x0F
                     : n == 1   ? 0x1F
                                : 0xFF;
    p++;
    for (int i = 0; i < n && (*p & 0xC0) == 0x80; i++) {
      c = c << 6 | (*p++ & 0x3F);
    }
    if (c < 0x80) {
      o += (char)c;
    } else if (c >= 0xC0 && c <= 0x17F) {
      o += LATIN[c - 0xC0];
      if (c == 0xDF)
        o += 's';  // ss
    } else if (c == 0x2018 || c == 0x2019) {
      o += '\'';
    } else if (c == 0x201C || c == 0x201D) {
      o += '"';
    } else if (c == 0x2013 || c == 0x2014) {
      o += '-';
    } else if (c == 0x2026) {
      o += "...";
    } else if (c == 0xA0) {
      o += ' ';
    } else if (c >= 0x1F000 || (c >= 0x2600 && c <= 0x27BF) || c == 0xFE0F) {
      // emoji: left out
    } else {
      o += '?';
    }
  }
  return o;
}

static void readEntry(JsonObjectConst e, Firmware& f) {
  f.name = ascii(e["name"] | "?");
  f.author = ascii(e["author"] | "");
  f.version = e["version"] | "";
  f.date = e["date"] | "";
  f.what = ascii(e["what"] | "");
  f.pkg = e["pkg"] | "";
  f.file = baseName(f.pkg);
  f.product = e["product"] | "";
  f.sha256 = e["sha256"] | "";
  f.size = e["size"] | 0;
  f.stars = e["stars"] | 0;
  f.installs = e["installs"] | 0;
  f.beta = e["beta"] | false;
  f.keyboard = e["hid"]["keyboard"] | false;
  f.mouse = e["hid"]["mouse"] | false;
  JsonVariantConst k = e["kind"];
  if (k.is<const char*>()) {
    f.kinds.push_back(k.as<const char*>());
  } else {
    for (JsonVariantConst v : k.as<JsonArrayConst>()) {
      f.kinds.push_back(v | "");
    }
  }
}

bool catalog::parseHub(std::vector<Firmware>& out, std::string& updated, std::string& err) {
  std::string text;
  if (!readAll(path(HUB_CACHE), text)) {
    err = "The Hub's list isn't on the SD card yet.";
    return false;
  }
  JsonDocument filter;
  filter["updated"] = true;
  for (const char* k : {"name", "author", "version", "date", "what", "pkg", "product", "sha256", "size", "stars",
                        "installs", "beta", "hid", "kind"}) {
    filter["firmwares"][0][k] = true;
  }
  for (const char* k : {"pkg", "product", "sha256", "size"}) {
    filter["stock"][k] = true;
  }
  JsonDocument doc;
  DeserializationError bad = deserializeJson(doc, text, DeserializationOption::Filter(filter));
  text = std::string();
  if (bad) {
    err = std::string("The Hub's list didn't read (") + bad.c_str() + ").";
    return false;
  }
  out.clear();
  updated = doc["updated"] | "";
  JsonObjectConst stock = doc["stock"];
  if (!stock.isNull()) {
    Firmware f;
    readEntry(stock, f);
    f.name = "M-VAVE stock";
    f.author = "M-VAVE";
    f.what = "The firmware the FM-1 came with: put it back to how it was.";
    f.kinds = {"stock"};
    f.stock = true;
    out.push_back(f);
  }
  for (JsonObjectConst e : doc["firmwares"].as<JsonArrayConst>()) {
    Firmware f;
    readEntry(e, f);
    if (!f.pkg.empty() && f.size && f.sha256.size() == 64 && f.pkg.find("..") == std::string::npos) {
      out.push_back(f);
    }
  }
  return true;
}

std::string catalog::product(const char* p) {
  // one marker byte after each of the first 20 blocks of 0x30: the device it's for
  const int BLOCKS = 20, BLK = 0x30, KEEP = 0x2F;
  uint8_t head[BLOCKS * BLK];
  FILE* f = fopen(p, "rb");
  if (!f) {
    return "";
  }
  bool ok = fread(head, 1, sizeof head, f) == sizeof head;
  fclose(f);
  std::string s;
  for (int i = 0; ok && i < BLOCKS; i++) {
    uint8_t m = head[i * BLK + KEEP];
    if (m != 0x7D) {
      char c = (char)((m - i - 1) & 0xFF);
      if (c < 0x20 || c > 0x7E) {
        return "";
      }
      s += c;
    }
  }
  return s;
}

static bool isPkg(const char* name) {
  size_t n = strlen(name);
  return name[0] != '.' && n > 5 && !strcasecmp(name + n - 5, ".fwsc");
}

void catalog::scanSd() {
  sd.clear();
  for (Firmware& h : hub) {
    h.onSd = false;
  }
  std::string dir = path(FW_DIR);
  DIR* d = dir.empty() ? 0 : opendir(dir.c_str());
  if (!d) {
    return;
  }
  while (dirent* e = readdir(d)) {
    if (!isPkg(e->d_name)) {
      continue;
    }
    std::string full = dir + "/" + e->d_name;
    Firmware f;
    auto h = std::find_if(hub.begin(), hub.end(), [&](const Firmware& x) { return !strcasecmp(x.file.c_str(), e->d_name); });
    if (h != hub.end()) {
      h->onSd = true;
      f = *h;
      f.hub = true;
    } else {
      f.name = e->d_name;
      f.name.resize(f.name.size() - 5);
    }
    f.onSd = true;
    f.file = e->d_name;
    struct stat st;
    if (!stat(full.c_str(), &st)) {
      f.size = st.st_size;
    }
    f.product = product(full.c_str());
    sd.push_back(f);
  }
  closedir(d);
  std::sort(sd.begin(), sd.end(), [](const Firmware& a, const Firmware& b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
}
