// What the app needs from the board (src/s3) or the simulator (src/sim). The slow calls (Wi-Fi, HTTP, sha256)
// block: the app makes them from a job (jobs.h), never from the UI.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace hal {
uint32_t millis();
// a line for the serial console (the terminal, in the simulator)
void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// the SD card's root, for fopen and opendir ("/sdcard"), or 0 without a card
const char* sd();
bool sdSpace(uint64_t& total, uint64_t& used);

// a small blob kept across restarts
bool load(const char* key, void* p, size_t n);
void save(const char* key, const void* p, size_t n);

void backlight(uint8_t percent);

// Wi-Fi is off unless a job needs it
struct Ap {
  std::string ssid;
  int rssi;
  bool open;
};
bool wifiScan(std::vector<Ap>& out);
bool wifiJoin(const char* ssid, const char* pass, std::string& err);
void wifiOff();
bool wifiOn();

// HTTPS GET url into path (written to path.part, renamed when it's all there). With etag: sends If-None-Match, and
// the new ETag comes back in it. progress may be 0. Returns the HTTP status (304: not changed, nothing written), or
// -1 (err says why)
int get(const char* url, const char* path, std::string* etag, void (*progress)(uint32_t done, uint32_t total),
        std::string& err);
bool sha256(const char* path, char hex[65]);

// run fn on a thread of its own
void spawn(void (*fn)(void*), void* arg);
}  // namespace hal
