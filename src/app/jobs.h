// The slow things, one at a time on a thread of their own: Wi-Fi is turned on for a job and off after it. The UI
// watches jobs::step and jobs::running, and reads the rest once running is false.
#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "catalog.h"
#include "hal.h"

namespace jobs {
enum Kind { NONE,
            HUB,       // update the Hub's list
            DOWNLOAD,  // a Hub firmware to /fm1
            SCAN,      // the Wi-Fi networks
            JOIN };    // try a Wi-Fi network (Settings)
enum Step { IDLE,
            JOINING,
            FETCHING,
            CHECKING,
            READING };

extern std::atomic<int> kind, step;
extern std::atomic<bool> running;
extern std::atomic<uint32_t> done, total;  // bytes, while FETCHING
// once it has finished
extern bool ok;
extern std::string error;
extern std::vector<hal::Ap> aps;   // SCAN
extern std::vector<Firmware> hub;  // HUB: the new list
extern std::string hubUpdated;

bool hubList();
bool download(const Firmware& f);
bool scan();
bool join(const char* ssid, const char* pass);
// "Joining Wi-Fi", "Downloading 40%", ...
std::string status();
}  // namespace jobs
