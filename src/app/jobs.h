// The slow things, one at a time on a thread of their own: Wi-Fi is turned on for a job, and stays on (MIDI routing
// turns it off: midi.h). The UI watches jobs::step and jobs::running, and reads the rest once running is false.
#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "catalog.h"
#include "hal.h"
#include "sounds.h"

namespace jobs {
enum Kind { NONE,
            HUB,       // update the Hub's list
            DOWNLOAD,  // a Hub firmware to /fm1
            SCAN,      // the Wi-Fi networks
            JOIN,      // try a Wi-Fi network (Settings)
            INSTALL,   // a firmware on the FM-1 (downloaded first, from the Hub, if it isn't on the SD card)
            PROBE,     // what a device's firmware speaks (sounds.h)
            SEND,      // a .syx to a device
            VOICE,     // the stock firmware's current voice to the SD card
            KEEP,      // what a firmware shares (fomajam's song) into a new setup
            RESTORE,   // a setup back to it
            EXPORT,    // a setup's patterns as MIDI files, its kits as .wav files
            BACKUP };  // all of the FM-1's flash to the SD card (through its boot loader: uboot.h)
enum Step { IDLE,
            JOINING,
            FETCHING,
            CHECKING,
            READING,
            ASKING,     // installing: which FM-1
            VERIFYING,  // the FM-1 reads parts of the package
            LOADER,     // it restarts into its update loader
            WRITING,    // the loader reads it all, and writes it
            RESTARTING,
            SENDING,
            ASKING_IT,
            GETTING,        // KEEP
            WRITING_FILES,  // EXPORT
            BOOT_LOADER,    // BACKUP: the FM-1 into its boot loader
            READING_FLASH,  // BACKUP: its flash (done, total), twice
            LEAVING };      // BACKUP: back to its firmware

extern std::atomic<int> kind, step;
extern std::atomic<bool> running;
extern std::atomic<uint32_t> finished;     // counts the jobs that have ended
extern std::atomic<uint32_t> done, total;  // bytes, while FETCHING and WRITING
// once it has finished
extern bool ok;
extern std::string error;
extern std::vector<hal::Ap> aps;   // SCAN
extern std::vector<Firmware> hub;  // HUB: the new list
extern std::string hubUpdated;
extern int device;             // PROBE, SEND, VOICE, KEEP, RESTORE: the device
extern sounds::Speaks speaks;  // PROBE
extern std::string saved;      // VOICE: the file it went into; KEEP: the setup; EXPORT: what it did; BACKUP: its file

bool hubList(bool asked = true);  // asked: the user did (else it's automatic, and doesn't pause Bluetooth MIDI)
// a job the user asked for needed Wi-Fi, so Bluetooth MIDI paused (once, for a message)
bool bluetoothPaused();
bool download(const Firmware& f);
bool scan();
bool join(const char* ssid, const char* pass);
bool install(const Firmware& f);
bool probe(int d);
bool send(int d, const std::string& file);  // in /syx
bool voice(int d);
bool keep(int d);
bool restore(int d, const std::string& setup);  // in /setups
bool exportSetup(const std::string& setup, const char* app);
bool backup(int d);  // device d: the FM-1
// "Joining Wi-Fi", "Downloading 40%", "Writing 12%", ...
std::string status();
}  // namespace jobs
