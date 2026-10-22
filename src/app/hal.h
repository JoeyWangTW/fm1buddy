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
bool sha256(const uint8_t* p, size_t n, char hex[65]);

// USB: the port is the host (a hub on it, the FM-1 and controllers on the hub), or, when a computer is on it at
// power-on, a serial port to the computer (for flashing and logs)
bool usbHost();
// up to n USB MIDI packets (4 bytes each) to device d (midi.h: USB or Bluetooth): how many it took. From
// midi::service
uint32_t midiOut(int d, const uint8_t* packets, uint32_t n);

// Bluetooth MIDI: off (not even started) until it's turned on (the app saves the switch). Phones and computers connect to
// "fm1buddy"; keyboards and synths are found while searching, and tapping one connects it (and remembers it, to
// connect again whenever it's around). Their MIDI is midi.h's devices 4-7. Call from the UI's thread
struct BlePeer {
  char name[24];
  int rssi;
  bool saved;
  int state;  // 0: not connected, 1: connecting, 2: connected
};
void bleOn(bool on);
bool bleIsOn();
void bleSearch(bool on);  // look for keyboards and synths all the time (the MIDI page shows)
int blePeers(BlePeer* out, int max);
void bleToggle(int i);  // connect and remember peer i, or forget and disconnect it
bool bleChanged();      // the list changed (once)
void bleHold(bool on);  // Bluetooth stops for a while (an FM-1 install), and comes back as it was

// run fn on a thread of its own
void spawn(void (*fn)(void*), void* arg);
}  // namespace hal
