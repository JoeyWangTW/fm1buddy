// SPDX-License-Identifier: GPL-3.0-only
#ifdef FM1BUDDY_STOCK_RESTORE
#include "guarded.h"

#include <Arduino.h>
#include <fcntl.h>
#include <mbedtls/sha256.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include "catalog.h"
#include "hal.h"
#include "jobs.h"
#include "policy.h"
#include "stock_restore.h"
#include "uboot.h"

namespace {
constexpr const char* BUNDLE = "/transporter/stock-v15.bundle";
struct Context {
  char backupHash[65]{};
  const uint8_t* snapshot{};
};
bool operationValid(const std::string& op) {
  return op == "backup" || op == "dryrun-v15" || op == "restore-v15";
}
bool loadStock(std::vector<uint8_t>& image, std::string& error) {
  std::string path = catalog::path(BUNDLE);
  struct stat st{};
  uint8_t header[112];
  FILE* f = fopen(path.c_str(), "rb");
  bool ok = f && !fstat(fileno(f), &st) && st.st_size == STOCK_BUNDLE_SIZE &&
            fread(header, 1, sizeof header, f) == sizeof header &&
            !memcmp(header, "FM1V15R1", 8) && le32_value(header + 104) == STOCK_IMAGE_SIZE &&
            le32_value(header + 108) == STOCK_FLASH_SIZE;
  if (ok) {
    image.resize(STOCK_IMAGE_SIZE);
    ok = fread(image.data(), 1, image.size(), f) == image.size();
  }
  if (f)
    fclose(f);
  char hash[65]{};
  ok = ok && hal::sha256(image.data(), image.size(), hash) && !strcmp(hash, STOCK_IMAGE_SHA);
  if (!ok) {
    image.clear();
    error = "Stock V15 bundle missing or invalid. Nothing written.";
  }
  // Only the pinned image is used. The old bundle's reference is deliberately
  // replaced by this attempt's two matching reads and verified fresh SD backup.
  return ok;
}
void progress(uint32_t done, uint32_t total) {
  jobs::done = done;
  jobs::total = total;
}
bool readRange(void*, uint32_t address, uint8_t* data, size_t n) {
  if (!data || !n || n > 4096 || address > STOCK_FLASH_SIZE - n)
    return false;
  uint8_t c[16];
  memset(c, 255, sizeof c);
  c[0] = 0xFD;
  c[1] = 5;
  c[2] = address >> 24;
  c[3] = address >> 16;
  c[4] = address >> 8;
  c[5] = address;
  c[6] = n >> 8;
  c[7] = n;
  uint32_t got = 0;
  return hal::storageCommand(c, 16, true, data, n, &got, 2500) && got == n;
}
bool authenticate(void* context, const uint8_t* image, const uint8_t* reference) {
  auto& c = *static_cast<Context*>(context);
  char hash[65]{};
  return reference == c.snapshot && hal::sha256(image, STOCK_IMAGE_SIZE, hash) &&
         !strcmp(hash, STOCK_IMAGE_SHA) && hal::sha256(reference, STOCK_FLASH_SIZE, hash) &&
         !strcmp(hash, c.backupHash);
}
bool saveVerified(const std::vector<uint8_t>& bytes, Context& c) {
  if (bytes.size() != STOCK_FLASH_SIZE || !hal::sha256(bytes.data(), bytes.size(), c.backupHash))
    return false;
  std::string dir = catalog::path(BACKUPS_DIR);
  mkdir(dir.c_str(), 0777);
  std::string partial, final;
  int fd = -1;
  for (unsigned i = 0; i < 16 && fd < 0; ++i) {
    char name[48];
    snprintf(name, sizeof name, "fm1-stock-%08lx.bin", (unsigned long)esp_random());
    final = dir + "/" + name;
    if (!access(final.c_str(), F_OK))
      continue;
    partial = final + ".partial";
    fd = open(partial.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
  }
  if (fd < 0)
    return false;
  FILE* f = fdopen(fd, "wb");
  if (!f) {
    close(fd);
    return false;
  }
  bool ok = true;
  for (size_t at = 0; ok && at < bytes.size(); at += 16384) {
    size_t n = std::min<size_t>(16384, bytes.size() - at);
    ok = fwrite(bytes.data() + at, 1, n, f) == n;
    uboot::keepAlive();
  }
  ok = !fflush(f) && ok;
  ok = !fsync(fileno(f)) && ok;
  ok = !fclose(f) && ok;
  uboot::keepAlive();
  f = ok ? fopen(partial.c_str(), "rb") : nullptr;
  if (!f)
    return false;
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  mbedtls_sha256_starts(&hash, 0);
  uint8_t chunk[4096], digest[32];
  size_t count = 0, n;
  while ((n = fread(chunk, 1, sizeof chunk, f))) {
    count += n;
    if (count > bytes.size() || mbedtls_sha256_update(&hash, chunk, n)) {
      ok = false;
      break;
    }
    uboot::keepAlive();
  }
  ok = !ferror(f) && count == bytes.size() && ok;
  ok = !fclose(f) && ok;
  ok = !mbedtls_sha256_finish(&hash, digest) && ok;
  mbedtls_sha256_free(&hash);
  char actual[65]{};
  for (unsigned i = 0; i < 32; ++i) snprintf(actual + 2 * i, 3, "%02x", digest[i]);
  if (!ok || strcmp(actual, c.backupHash) || rename(partial.c_str(), final.c_str()))
    return false;
  jobs::saved = final.substr(dir.size() + 1);
  FILE* meta = fopen((final + ".txt").c_str(), "w");
  ok = meta && fprintf(meta, "firmware before guarded stock recovery (two matching reads)\nsize 1048576\nsha256 %s\n", c.backupHash) > 0;
  if (meta) {
    ok = !fflush(meta) && ok;
    ok = !fsync(fileno(meta)) && ok;
    ok = !fclose(meta) && ok;
  }
  hal::log("stock: fresh backup %s sha256 %s verified=%d", jobs::saved.c_str(), c.backupHash, ok);
  return ok;
}
}  // namespace
float guarded::batteryVolts() {
  // Arduino must allocate the ADC channel before per-pin attenuation is set.
  (void)analogRead(9);
  analogSetPinAttenuation(9, ADC_11db);
  uint32_t mv = 0;
  for (int i = 0; i < 16; ++i) mv += analogReadMilliVolts(9);
  return mv / 16.0f * 2.0f / 1000.0f;
}
bool guarded::powerReady(std::string& error) {
  float v = batteryVolts();
  if (v >= 3.90f && v <= 4.35f)
    return true;
  char message[100];
  snprintf(message, sizeof message, "ESP32 battery %.2f V: charge to at least 3.90 V before flashing.", v);
  error = message;
  return false;
}
bool guarded::preflight(const std::string& op, std::string& error) {
  if (!operationValid(op)) {
    error = "Choose backup, V15 dry run or stock V15 restore.";
    return false;
  }
  if (!hal::sd()) {
    error = "No SD card.";
    return false;
  }
  if (op == "backup")
    return true;
  std::vector<uint8_t> image;
  return powerReady(error) && loadStock(image, error);
}
bool guarded::run(const std::string& op) {
  using namespace jobs;
  saved.clear();
  error.clear();
  step = GETTING_READY;
  const bool write = op == "restore-v15";
  if (hal::onComputer() || !operationValid(op)) {
    error = "Recovery requires battery mode and a new on-screen selection.";
    return false;
  }
  // Consume the persisted action before touching the FM-1. A stale RTC restart
  // cannot repeat a restore; a fresh selection and confirmation are required.
  char empty[160]{}, check[160]{};
  hal::save("recover", empty, sizeof empty);
  if (!hal::load("recover", check, sizeof check) || memcmp(empty, check, sizeof check)) {
    error = "Could not consume recovery selection. Nothing written.";
    return false;
  }
  if (!preflight(op, error))
    return false;
  std::vector<uint8_t> image, first, second;
  if (op != "backup" && !loadStock(image, error))
    return false;
  step = KEYING;
  hal::keyStart();
  uint32_t start = hal::millis();
  while (hal::keyState() != hal::KEY_HOST) {
    if (hal::keyState() == hal::KEY_FAILED || hal::millis() - start >= 65000) {
      error = "Recovery key or USB detection failed. Nothing written; save the log.";
      return false;
    }
    delay(50);
  }
  step = BOOT_LOADER;
  uboot::Chip chip{};
  const char* e = uboot::attach(chip);
  if (!e && (chip.key != FM1_KEY || chip.flashId != FM1_FLASH || chip.size != STOCK_FLASH_SIZE))
    e = "Unexpected chip identity. Nothing written.";
  step = CHECKING_FLASH;
  if (!e)
    e = uboot::read(chip, first, progress);
  if (!e)
    e = uboot::read(chip, second, progress);
  if (!e && first != second)
    e = "Flash reads differ. Nothing written.";
  std::vector<uint8_t>().swap(second);
  Context ctx;
  ctx.snapshot = first.data();
  step = SAVING;
  if (!e && !saveVerified(first, ctx))
    e = "Fresh SD backup verification failed. Nothing written.";
  StockResult result{};
  if (!e && op != "backup") {
    StockOps ops{};
    ops.ctx = &ctx;
    ops.authenticate = authenticate;
    ops.read = readRange;
    ops.write = [](void*, uint32_t a, const uint8_t* p) { return uboot::writeSector(a, p) == nullptr; };
    ops.may_continue = [](void*) {
      std::string why;
      return guarded::powerReady(why);
    };
    ops.progress = [](void*, uint32_t a, unsigned d, unsigned t) {
      jobs::step = WRITING_FLASH;
      progress(d, t);
      hal::log("stock: sector %06lx verified %u/%u", (unsigned long)a, d, t);
    };
    step = CHECKING_FLASH;
    // The verified fresh snapshot binds this attempt; the engine re-reads all
    // live flash again before opening its first one-sector command window.
    bool ok = stock_execute(image.data(), first.data(), first.data(), write, &ops, &result);
    hal::log("stock: %s result=%s sectors=%u started=%u address=%06lx", op.c_str(),
             ok ? "PASS" : result.error, result.sectors, result.write_started, (unsigned long)result.address);
    if (!ok)
      e = result.error;
  }
  if (e && result.write_started) {
    error = std::string(e) + ". KEEP BOTH DEVICES POWERED. Do not unplug, reset or retry; save the screen message.";
    hal::log("stock: %s", error.c_str());
    step = RECOVERY_HOLD;
    // No RUN_APP, no retry, no restart button after an ambiguous flash write.
    // The transport remains closed after an error; heartbeat is best effort.
    for (;;) {
      uboot::keepAlive();
      delay(500);
    }
  }
  step = LEAVING;
  uboot::leave();
  if (e) {
    error = e;
    return false;
  }
  error = op == "backup" ? "Backup verified. Nothing written." : write ? "Stock V15 restored and full flash verified."
                                                                       : "V15 dry run passed. Nothing written.";
  hal::log("stock: %s", error.c_str());
  return true;
}
#endif
