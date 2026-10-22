// The board, pretended: a folder for the SD card, files for the settings, pretend Wi-Fi (any password works but
// "wrong"), and the real internet through libcurl.
#include <CommonCrypto/CommonDigest.h>
#include <curl/curl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <chrono>
#include <string>
#include <thread>

#include "hal.h"

static bool wifi;

uint32_t hal::millis() {
  using namespace std::chrono;
  static auto t0 = steady_clock::now();
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

void hal::log(const char* fmt, ...) {
  va_list a;
  va_start(a, fmt);
  vprintf(fmt, a);
  va_end(a);
  putchar('\n');
  fflush(stdout);
}

const char* hal::sd() {
  static std::string root;
  if (root.empty()) {
    const char* e = getenv("FM1_SD");
    root = e ? e : "sim/sd";
    mkdir(root.c_str(), 0777);
  }
  struct stat st;
  return stat(root.c_str(), &st) ? 0 : root.c_str();
}

bool hal::sdSpace(uint64_t& total, uint64_t& used) {
  struct statfs s;
  if (statfs(sd(), &s)) {
    return false;
  }
  total = (uint64_t)s.f_blocks * s.f_bsize;
  used = total - (uint64_t)s.f_bavail * s.f_bsize;
  return true;
}

static std::string blob(const char* key) {
  mkdir("sim", 0777);
  return std::string("sim/") + key + ".bin";
}

bool hal::load(const char* key, void* p, size_t n) {
  FILE* f = fopen(blob(key).c_str(), "rb");
  if (!f) {
    return false;
  }
  bool ok = fread(p, 1, n, f) == n;
  fclose(f);
  return ok;
}

void hal::save(const char* key, const void* p, size_t n) {
  if (FILE* f = fopen(blob(key).c_str(), "wb")) {
    fwrite(p, 1, n, f);
    fclose(f);
  }
}

void hal::backlight(uint8_t) {}

bool hal::wifiScan(std::vector<Ap>& out) {
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  out = {{"Studio", -48, false}, {"Neighbours 5G", -71, false}, {"Cafe", -80, true}};
  return true;
}

bool hal::wifiJoin(const char* ssid, const char* pass, std::string& err) {
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  if (!strcmp(pass, "wrong")) {
    err = std::string("Can't join ") + ssid + ": check the password.";
    return false;
  }
  wifi = true;
  return true;
}

void hal::wifiOff() {
  wifi = false;
}

bool hal::wifiOn() {
  return wifi;
}

struct Get {
  FILE* f;
  std::string* etag;
  void (*progress)(uint32_t, uint32_t);
};

static size_t onData(char* p, size_t s, size_t n, void* u) {
  return fwrite(p, s, n, ((Get*)u)->f);
}

static size_t onHeader(char* p, size_t s, size_t n, void* u) {
  Get* g = (Get*)u;
  std::string h(p, s * n);
  if (g->etag && !strncasecmp(h.c_str(), "etag:", 5)) {
    size_t a = h.find_first_not_of(" \t", 5), b = h.find_last_not_of("\r\n");
    *g->etag = h.substr(a, b - a + 1);
  }
  return s * n;
}

static int onProgress(void* u, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
  Get* g = (Get*)u;
  if (g->progress) {
    g->progress((uint32_t)now, (uint32_t)total);
  }
  return 0;
}

int hal::get(const char* url, const char* path, std::string* etag, void (*progress)(uint32_t, uint32_t),
             std::string& err) {
  std::string part = std::string(path) + ".part";
  Get g = {fopen(part.c_str(), "wb"), etag, progress};
  if (!g.f) {
    err = "Can't write to the SD card.";
    return -1;
  }
  CURL* c = curl_easy_init();
  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "fm1buddy");
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onData);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &g);
  curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, onHeader);
  curl_easy_setopt(c, CURLOPT_HEADERDATA, &g);
  curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
  curl_easy_setopt(c, CURLOPT_XFERINFODATA, &g);
  curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
  struct curl_slist* hs = 0;
  if (etag && !etag->empty()) {
    hs = curl_slist_append(hs, ("If-None-Match: " + *etag).c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hs);
  }
  CURLcode r = curl_easy_perform(c);
  long code = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
  curl_slist_free_all(hs);
  curl_easy_cleanup(c);
  fclose(g.f);
  if (r != CURLE_OK) {
    err = std::string("Can't reach it: ") + curl_easy_strerror(r);
    remove(part.c_str());
    return -1;
  }
  if (code != 200) {
    remove(part.c_str());
    return (int)code;
  }
  remove(path);
  if (rename(part.c_str(), path)) {
    err = "Can't rename it on the SD card.";
    return -1;
  }
  return 200;
}

bool hal::sha256(const uint8_t* p, size_t n, char hex[65]) {
  uint8_t sum[32];
  CC_SHA256(p, (CC_LONG)n, sum);
  for (int i = 0; i < 32; i++) {
    sprintf(hex + i * 2, "%02x", sum[i]);
  }
  return true;
}

bool hal::sha256(const char* path, char hex[65]) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    return false;
  }
  CC_SHA256_CTX ctx;
  CC_SHA256_Init(&ctx);
  uint8_t buf[4096];
  for (size_t n; (n = fread(buf, 1, sizeof buf, f));) {
    CC_SHA256_Update(&ctx, buf, (CC_LONG)n);
  }
  fclose(f);
  uint8_t sum[32];
  CC_SHA256_Final(sum, &ctx);
  for (int i = 0; i < 32; i++) {
    sprintf(hex + i * 2, "%02x", sum[i]);
  }
  return true;
}

void hal::spawn(void (*fn)(void*), void* arg) {
  std::thread(fn, arg).detach();
}
