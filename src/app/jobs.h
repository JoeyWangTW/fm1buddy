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
            HUB,        // update the Hub's list
            DOWNLOAD,   // a Hub firmware to /fm1
            SCAN,       // the Wi-Fi networks
            JOIN,       // try a Wi-Fi network (Settings)
            INSTALL };  // a firmware on the FM-1 (downloaded first, from the Hub, if it isn't on the SD card)
enum Step { IDLE,
            JOINING,
            FETCHING,
            CHECKING,
            READING,
            ASKING,     // installing: which FM-1
            VERIFYING,  // the FM-1 reads parts of the package
            LOADER,     // it restarts into its update loader
            WRITING,    // the loader reads it all, and writes it
            RESTARTING };

extern std::atomic<int> kind, step;
extern std::atomic<bool> running;
extern std::atomic<uint32_t> done, total;  // bytes, while FETCHING and WRITING
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
bool install(const Firmware& f);
// "Joining Wi-Fi", "Downloading 40%", "Writing 12%", ...
std::string status();
}  // namespace jobs
