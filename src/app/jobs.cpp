#include "jobs.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <thread>

#include "fm1.h"
#include "midi.h"
#include "settings.h"
#include "uboot.h"

namespace jobs {
std::atomic<int> kind{NONE}, step{IDLE};
std::atomic<bool> running{false};
std::atomic<uint32_t> finished{0};
std::atomic<uint32_t> done{0}, total{0};
bool ok;
std::string error;
std::vector<hal::Ap> aps;
std::vector<Firmware> hub;
std::string hubUpdated;
int device = -1;
sounds::Speaks speaks;
std::string saved;
}  // namespace jobs

using namespace jobs;

static Firmware target;
static std::string ssid, pass, syxFile, setupName;

static void progress(uint32_t d, uint32_t t) {
  done = d;
  total = t;
}

static const char* NAMES[] = {"none", "hub", "download", "scan", "join", "install", "probe", "send", "voice", "keep", "restore", "export", "backup"};

static bool start(Kind k, void (*fn)(void*)) {
  if (running) {
    hal::log("job: %s: busy with %s", NAMES[k], NAMES[(int)kind]);
    return false;
  }
  hal::log("job: %s", NAMES[k]);
  kind = k;
  step = IDLE;
  done = total = 0;
  ok = false;
  error.clear();
  running = true;
  hal::spawn(fn, 0);
  return true;
}

static void finish(bool good) {
  hal::log("job: %s %s%s%s", NAMES[(int)kind], good ? "done" : "failed", good ? "" : ": ", good ? "" : error.c_str());
  ok = good;
  step = IDLE;
  running = false;
  finished++;
}

static std::atomic<bool> routingWent{false};
static bool hubAsked = true;

bool jobs::routingStopped() {
  return routingWent.exchange(false);
}

// Wi-Fi for a job: it stays on once it's joined. MIDI routing goes off for it if the user asked (they can't both run);
// an automatic job doesn't stop it
static bool wifiFor(bool asked) {
  if (!midi::routing) {
    return true;
  }
  if (!asked) {
    error = "MIDI routing is on, so Wi-Fi is off.";
    return false;
  }
  midi::route(false);
  routingWent = true;
  return true;
}

static bool joinSaved(bool asked = true) {
  if (!wifiFor(asked)) {
    return false;
  }
  if (hal::wifiOn()) {
    return true;  // (still on)
  }
  if (!settings.ssid[0]) {
    error = "No Wi-Fi network yet: pick one in Settings.";
    return false;
  }
  step = JOINING;
  hal::log("job: joining %s", settings.ssid);
  bool ok = hal::wifiJoin(settings.ssid, settings.pass, error);
  hal::log("job: %s", ok ? "joined" : "not joined");
  return ok;
}

static void mkdirs() {
  mkdir(catalog::path(FW_DIR).c_str(), 0777);
  mkdir(catalog::path(DATA_DIR).c_str(), 0777);
}

static std::string readText(const std::string& p) {
  char buf[128] = "";
  if (FILE* f = fopen(p.c_str(), "r")) {
    buf[fread(buf, 1, sizeof buf - 1, f)] = 0;
    fclose(f);
  }
  return buf;
}

static void hubJob(void*) {
  if (!hal::sd()) {
    error = "No SD card: the list is kept on it.";
    return finish(false);
  }
  if (!joinSaved(hubAsked)) {
    return finish(false);
  }
  mkdirs();
  step = FETCHING;
  std::string etagPath = catalog::path(HUB_ETAG), cache = catalog::path(HUB_CACHE);
  struct stat st;
  std::string etag = stat(cache.c_str(), &st) ? "" : readText(etagPath);
  hal::log("job: getting the list (etag %s)", etag.c_str());
  int code = hal::get(HUB_SITE "catalog.json", cache.c_str(), &etag, progress, error);
  hal::log("job: HTTP %d", code);
  if (code != 200 && code != 304) {
    if (code > 0) {
      error = "The Hub said " + std::to_string(code) + ".";
    }
    return finish(false);
  }
  if (code == 200) {
    if (FILE* f = fopen(etagPath.c_str(), "w")) {
      fputs(etag.c_str(), f);
      fclose(f);
    }
  }
  step = READING;
  finish(catalog::parseHub(hub, hubUpdated, error));
}

// target from the Hub into /fm1: false (error says why)
static bool fetch() {
  if (!hal::sd()) {
    error = "No SD card.";
    return false;
  }
  if (!joinSaved()) {
    return false;
  }
  mkdirs();
  step = FETCHING;
  std::string p = catalog::path(std::string(FW_DIR "/") + target.file);
  hal::log("job: getting %s", target.pkg.c_str());
  int code = hal::get((HUB_SITE + target.pkg).c_str(), p.c_str(), 0, progress, error);
  hal::log("job: HTTP %d", code);
  if (code != 200) {
    if (code > 0) {
      error = "The Hub said " + std::to_string(code) + ".";
    }
    return false;
  }
  step = CHECKING;
  struct stat st;
  char sum[65];
  if (stat(p.c_str(), &st) || (uint32_t)st.st_size != target.size) {
    error = "The download stopped part way.";
  } else if (!hal::sha256(p.c_str(), sum) || strcasecmp(sum, target.sha256.c_str())) {
    error = "The download isn't what the Hub says it is (sha256).";
  } else if (catalog::product(p.c_str()) != target.product) {
    error = "The package isn't for the device the Hub says.";
  } else {
    return true;
  }
  remove(p.c_str());
  return false;
}

static void downloadJob(void*) {
  finish(fetch());
}

static void installProgress(Fm1Step s, uint32_t, uint32_t addr) {
  static const Step STEPS[] = {ASKING, VERIFYING, LOADER, WRITING, RESTARTING, RESTARTING};
  step = STEPS[s];
  done = s == STEP_WRITE ? addr : 0;
}

// a new install record: /fm1buddy/installs/N.txt, the last 10 kept. name gets "installs/N.txt"
static FILE* newRecord(std::string& name) {
  std::string dir = catalog::path(INSTALLS_DIR);
  mkdir(dir.c_str(), 0777);
  int last = 0;
  if (DIR* d = opendir(dir.c_str())) {
    while (dirent* e = readdir(d)) {
      last = std::max(last, atoi(e->d_name));
    }
    closedir(d);
  }
  for (int n = last - 9; n > 0 && n > last - 30; n--) {
    remove((dir + "/" + std::to_string(n) + ".txt").c_str());
  }
  name = "installs/" + std::to_string(last + 1) + ".txt";
  FILE* f = fopen((dir + "/" + std::to_string(last + 1) + ".txt").c_str(), "w");
  if (f) {
    setvbuf(f, 0, _IOFBF, 8192);
  }
  return f;
}

static void installJob(void*) {
  if (!target.onSd && !fetch()) {
    return finish(false);
  }
  step = CHECKING;
  Pkg pkg;
  if (const char* e = pkg.open(catalog::path(std::string(FW_DIR "/") + target.file).c_str())) {
    error = e;
    return finish(false);
  }
  if (!target.sha256.empty() && strcasecmp(pkg.sha256, target.sha256.c_str())) {
    error = "The file on the SD card isn't what the Hub says it is (sha256): download it again.";
    return finish(false);
  }
  std::string name;
  FILE* rec = newRecord(name);
  if (rec) {
    fprintf(rec, "fm1buddy install\n");
    fprintf(rec, "file     %s\n", target.file.c_str());
    fprintf(rec, "size     %u (image %u)\n", (unsigned)pkg.fileSize, (unsigned)pkg.size);
    fprintf(rec, "product  %s\n", pkg.product);
    fprintf(rec, "sha256   %s\n", pkg.sha256);
    fprintf(rec, "known    %s\n\n",
            target.sha256.empty() ? "no (not on the Hub, nor one of M-VAVE's): not checked" : "yes: its sha256 checked");
    fflush(rec);
  }
  total = pkg.size;
  step = ASKING;
  hal::bleHold(true);  // the radio stays out of it
  const char* e = fm1::install(pkg, installProgress, rec);
  hal::bleHold(false);
  if (rec) {
    fclose(rec);
  }
  if (e) {
    error = e;
    if (rec) {
      error += " (details: " DATA_DIR "/" + name + ")";
    }
  }
  finish(!e);
}

static void scanJob(void*) {
  aps.clear();
  wifiFor(true);
  bool good = hal::wifiScan(aps);
  if (!good) {
    error = "Wi-Fi didn't scan.";
  }
  finish(good);
}

static void joinJob(void*) {
  step = JOINING;
  wifiFor(true);
  finish(hal::wifiJoin(ssid.c_str(), pass.c_str(), error));
}

bool jobs::hubList(bool asked) {
  if (running) {
    return false;
  }
  hubAsked = asked;
  return start(HUB, hubJob);
}

bool jobs::download(const Firmware& f) {
  if (running) {
    return false;
  }
  target = f;
  return start(DOWNLOAD, downloadJob);
}

bool jobs::install(const Firmware& f) {
  if (running) {
    return false;
  }
  target = f;
  return start(INSTALL, installJob);
}

static void probeJob(void*) {
  step = ASKING_IT;
  sounds::probe(device, speaks);
  finish(true);
}

static void sendJob(void*) {
  std::vector<uint8_t> data;
  if (!sounds::load(syxFile, data)) {
    error = "Can't read it from the SD card.";
    return finish(false);
  }
  step = SENDING;
  total = data.size();
  if (const char* e = sounds::send(device, data, progress)) {
    error = e;
    return finish(false);
  }
  finish(true);
}

static void voiceJob(void*) {
  step = ASKING_IT;
  std::vector<uint8_t> syx;
  if (const char* e = sounds::readVoice(device, syx)) {
    error = e;
    return finish(false);
  }
  saved = sounds::save(syx, "voice");
  if (saved.empty()) {
    error = "Can't write it to the SD card.";
  }
  finish(!saved.empty());
}

static int phase;  // KEEP, RESTORE: where it's at (sounds.h)

static void keepJob(void*) {
  step = GETTING;
  total = 0;
  if (const char* e = sounds::keep(device, saved, progress, &phase)) {
    error = e;
    return finish(false);
  }
  finish(true);
}

static void restoreJob(void*) {
  step = SENDING;
  total = 0;
  if (const char* e = sounds::restore(device, setupName, progress, &phase)) {
    error = e;
    return finish(false);
  }
  finish(true);
}

// ------------------------------------------------------------------------------------------ backup ---
static int reading;  // BACKUP: which reading of its flash (it's read twice, and they must be the same)

static void backupJob(void*) {
  if (!hal::sd()) {
    error = "No SD card: the backup goes on it.";
    return finish(false);
  }
  mkdirs();
  if (!uboot::haveLoader()) {  // JieLi's loader: once, from jl-uboot-tool
    if (!joinSaved()) {
      return finish(false);
    }
    step = FETCHING;
    int code = hal::get(UBOOT_LOADER_URL, catalog::path(UBOOT_LOADER).c_str(), 0, progress, error);
    if (code != 200 || !uboot::haveLoader()) {
      remove(catalog::path(UBOOT_LOADER).c_str());
      if (code == 200 || error.empty()) {
        error = "JieLi's loader didn't download as it should.";
      }
      return finish(false);
    }
  }
  const Fm1Id* id = fm1::known(device);
  std::string was = id ? id->text : "";
  step = BOOT_LOADER;
  hal::bleHold(true);
  uboot::Chip chip;
  const char* e = uboot::enter(device, chip);
  std::vector<uint8_t> a, b;
  if (!e) {
    step = READING_FLASH;
    reading = 1;
    e = uboot::read(chip, a, progress);
  }
  if (!e) {
    reading = 2;
    e = uboot::read(chip, b, progress);
    if (!e && a != b) {
      e = "It read differently twice: try again.";
    }
  }
  step = LEAVING;
  uboot::leave();
  for (uint32_t start = hal::millis(); uboot::stuck() && hal::millis() - start < 3000;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  hal::bleHold(false);
  if (e) {
    error = std::string(e) + (uboot::stuck() ? " Switch the FM-1 off and on to get it back." : "");
    return finish(false);
  }
  // backups/fm1-N.bin, and what it was (fm1-N.txt)
  std::string dir = catalog::path(BACKUPS_DIR);
  mkdir(dir.c_str(), 0777);
  struct stat st;
  int n = 1;
  while (n < 1000 && !stat((dir + "/fm1-" + std::to_string(n) + ".bin").c_str(), &st)) {
    n++;
  }
  saved = "fm1-" + std::to_string(n) + ".bin";
  char sha[65] = "";
  hal::sha256(a.data(), a.size(), sha);
  FILE* f = fopen((dir + "/" + saved).c_str(), "wb");
  bool ok = f && fwrite(a.data(), 1, a.size(), f) == a.size();
  ok &= f && !fclose(f);
  if (ok) {
    f = fopen((dir + "/fm1-" + std::to_string(n) + ".txt").c_str(), "w");
    ok = f && fprintf(f,
                      "fm1buddy backup: all of the FM-1's flash, through its boot loader\n"
                      "firmware %s\nchip key %04X\nflash    %06X, %u bytes\nsha256   %s\n",
                      was.empty() ? "?" : was.c_str(), chip.key, (unsigned)chip.flashId, (unsigned)chip.size, sha) > 0;
    ok &= f && !fclose(f);
  }
  if (!ok) {
    error = "Can't write it to the SD card.";
  }
  finish(ok);
}

bool jobs::backup(int d) {
  if (running) {
    return false;
  }
  device = d;
  reading = 0;
  return start(BACKUP, backupJob);
}

static std::string exportApp;

static void exportJob(void*) {
  step = WRITING_FILES;
  const char* e = sounds::exportSetup(setupName, exportApp.c_str(), saved);
  if (e) {
    error = e;
  }
  finish(!e);
}

bool jobs::exportSetup(const std::string& setup, const char* app) {
  if (running) {
    return false;
  }
  setupName = setup;
  exportApp = app;
  return start(EXPORT, exportJob);
}

bool jobs::keep(int d) {
  if (running) {
    return false;
  }
  device = d;
  phase = 0;
  return start(KEEP, keepJob);
}

bool jobs::restore(int d, const std::string& setup) {
  if (running) {
    return false;
  }
  device = d;
  setupName = setup;
  phase = 0;
  return start(RESTORE, restoreJob);
}

bool jobs::probe(int d) {
  if (running) {
    return false;
  }
  device = d;
  return start(PROBE, probeJob);
}

bool jobs::send(int d, const std::string& file) {
  if (running) {
    return false;
  }
  device = d;
  syxFile = file;
  return start(SEND, sendJob);
}

bool jobs::voice(int d) {
  if (running) {
    return false;
  }
  device = d;
  return start(VOICE, voiceJob);
}

bool jobs::scan() {
  return start(SCAN, scanJob);
}

bool jobs::join(const char* s, const char* p) {
  if (running) {
    return false;
  }
  ssid = s;
  pass = p;
  return start(JOIN, joinJob);
}

std::string jobs::status() {
  switch ((int)step) {
    case JOINING:
      return "Joining " + std::string(kind == JOIN ? ssid : settings.ssid) + "...";
    case FETCHING: {
      if (kind == HUB) {
        return "Getting the Hub's list...";
      }
      if (kind == INSTALL) {
        uint32_t t = total, d = done;
        return "Downloading" + (t ? " " + std::to_string((unsigned long long)d * 100 / t) + "%" : std::string("..."));
      }
      uint32_t t = total, d = done;
      return t ? "Downloading " + std::to_string((unsigned long long)d * 100 / t) + "%" : "Downloading...";
    }
    case CHECKING:
      return "Checking it...";
    case ASKING:
      return "Looking for the FM-1...";
    case VERIFYING:
      return "The FM-1 is checking the package...";
    case LOADER:
      return "The FM-1 is starting its update loader...";
    case WRITING: {
      uint32_t t = total, d = done;
      return "Writing " + std::to_string(t ? (unsigned long long)d * 100 / t : 0) + "%: keep the FM-1 plugged in";
    }
    case RESTARTING:
      return "The FM-1 is restarting...";
    case READING:
      return "Reading the list...";
    case SENDING: {
      uint32_t t = total, d = done;
      std::string pct = std::to_string(t ? (unsigned long long)d * 100 / t : 0) + "%...";
      if (kind == RESTORE) {
        return phase == 0 ? "Making the kits..." : (phase == 1 ? "Sending the kits " : "Sending the song ") + pct;
      }
      return "Sending " + pct;
    }
    case ASKING_IT:
      return "Asking it...";
    case WRITING_FILES:
      return "Writing the files...";
    case BOOT_LOADER:
      return "Into its boot loader...";
    case READING_FLASH: {
      uint32_t t = total, d = done;
      return std::string(reading == 1 ? "Reading it " : "Reading it again ") +
             std::to_string(t ? (unsigned long long)d * 100 / t : 0) + "%...";
    }
    case LEAVING:
      return "Back to its firmware...";
    case GETTING: {
      uint32_t t = total, d = done;
      return (phase ? "Getting the kits " : "Getting the song ") + std::to_string(t ? (unsigned long long)d * 100 / t : 0) +
             "%...";
    }
  }
  return kind == SCAN && running ? "Looking for networks..." : "";
}
