// Tests the adapted policy with the actual fm1buddy writer.
#include "stock_restore.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/s3/recovery/policy.h"
#include "hal.h"
#include "uboot.h"
static uint8_t image[STOCK_IMAGE_SIZE], ref[STOCK_FLASH_SIZE], snapshot[STOCK_FLASH_SIZE],
    flash[STOCK_FLASH_SIZE];
static unsigned erases, writes, reads, may_calls, stop_after, fail_write, short_read;
static uint32_t addresses[143];
static bool authentic, corrupt_write, damage_tail;
namespace hal {
void log(const char *, ...) {}
}  // namespace hal
static void put32(uint8_t *b, uint32_t v) {
  b[0] = v >> 24;
  b[1] = v >> 16;
  b[2] = v >> 8;
  b[3] = v;
}
bool hal::storageCommand(const uint8_t *c, uint8_t n, bool in, uint8_t *data, uint32_t len, uint32_t *got, uint32_t) {
  if (c[0] == 0xFB && c[1] == 4) {
    uint8_t bad[512], wrong[16];
    memcpy(bad, data, 512);
    bad[0] ^= 1;
    assert(!stock_guard_command(c, n, in, bad, len));
    memcpy(wrong, c, 16);
    wrong[9] ^= 1;
    assert(!stock_guard_command(wrong, n, in, data, len));
  }
  assert(command_allowed(c, n, in, len) || stock_guard_command(c, n, in, data, len));
  unsigned op = (unsigned)c[0] << 8 | c[1];
  uint32_t a = be32(c + 2);
  if (op == 0xFB01) {
    assert(a >= STOCK_WRITE_MIN && a < STOCK_IMAGE_SIZE && !(a % 4096));
    addresses[erases++] = a;
    memset(flash + a, 255, 4096);
  } else if (op == 0xFB04) {
    ++writes;
    if (fail_write && writes == fail_write)
      return false;
    memcpy(flash + a, data, len);
    if (corrupt_write && (a % 4096) == 3584)
      flash[a] ^= 1;
    if (damage_tail)
      flash[STOCK_IMAGE_SIZE] = 0;
  } else if (op == 0xFD05) {
    ++reads;
    memcpy(data, flash + a, len);
    if (got)
      *got = short_read && reads == short_read ? len - 1 : len;
    return true;
  }
  if (in) {
    memset(data, 0, len);
    ((uint8_t *)data)[0] = c[0];
    ((uint8_t *)data)[1] = c[1];
  }
  if (got)
    *got = len;
  return true;
}
static bool auth(void *ctx, const uint8_t *i, const uint8_t *r) {
  (void)ctx;
  (void)i;
  (void)r;
  return authentic;
}
static bool read_cb(void *ctx, uint32_t a, uint8_t *p, size_t n) {
  (void)ctx;
  uint8_t c[16];
  memset(c, 255, sizeof c);
  c[0] = 0xFD;
  c[1] = 5;
  put32(c + 2, a);
  c[6] = n >> 8;
  c[7] = n;
  uint32_t got = 0;
  return hal::storageCommand(c, 16, true, p, n, &got, 2000) && got == n;
}
static bool write_cb(void *ctx, uint32_t a, const uint8_t *p) {
  (void)ctx;
  // Even an open sector window rejects unrelated addresses, malformed commands,
  // writes before erase, wrong payloads, and duplicate erase after completion.
  uint8_t c[16];
  memset(c, 255, sizeof c);
  c[0] = 0xFB;
  c[1] = 1;
  put32(c + 2, 0);
  assert(!stock_guard_command(c, 16, true, NULL, 16));
  put32(c + 2, a);
  c[6] = 0;
  assert(!stock_guard_command(c, 16, true, NULL, 16));
  c[1] = 4;
  c[6] = 2;
  c[7] = 0;
  c[8] = 0;
  assert(!stock_guard_command(c, 16, false, p, 512));
  bool ok = uboot::writeSector(a, p) == nullptr;
  memset(c, 255, sizeof c);
  c[0] = 0xFB;
  c[1] = 1;
  put32(c + 2, a);
  assert(!stock_guard_command(c, 16, true, NULL, 16));
  return ok;
}
static bool may(void *ctx) {
  (void)ctx;
  return ++may_calls <= stop_after;
}
static StockOps ops = {NULL, auth, read_cb, write_cb, may, NULL};
static void init(void) {
  memset(ref, 0x5a, sizeof ref);
  memcpy(image, ref, sizeof image);
  memset(image + 0x4000, 0x23, 4096);
  memset(image + 0x10000, 0x42, 4096);
  memset(image + 0x92000, 0x86, 4096);
  memcpy(snapshot, ref, sizeof ref);
  snapshot[0x94000] = 0x88;
  memcpy(flash, snapshot, sizeof flash);
  erases = writes = reads = may_calls = fail_write = short_read = 0;
  stop_after = 10000;
  authentic = true;
  corrupt_write = damage_tail = false;
}
int main(void) {
  StockResult r;
  init();
  assert(stock_execute(image, ref, snapshot, false, &ops, &r));
  assert(r.sectors == 3 && r.data_changed == 1 && erases == 0 && writes == 0 && !r.write_started);
  init();
  authentic = false;
  assert(!stock_execute(image, ref, snapshot, true, &ops, &r));
  assert(!reads && !erases);
  init();
  image[0] ^= 1;
  assert(!stock_execute(image, ref, snapshot, true, &ops, &r));
  assert(!erases && !reads);
  init();
  snapshot[0x5000] ^= 1;
  assert(!stock_execute(image, ref, snapshot, true, &ops, &r));
  assert(!erases && !reads);
  init();
  flash[0x7000] ^= 1;
  assert(!stock_execute(image, ref, snapshot, true, &ops, &r));
  assert(!erases);
  init();
  short_read = 30;
  assert(!stock_execute(image, ref, snapshot, true, &ops, &r));
  assert(!erases);
  init();
  stop_after = 0;
  assert(!stock_execute(image, ref, snapshot, true, &ops, &r));
  assert(!erases);
  init();
  fail_write = 3;
  assert(!stock_execute(image, ref, snapshot, true, &ops, &r));
  assert(erases == 1 && writes == 3 && r.write_started);
  init();
  corrupt_write = true;
  assert(!stock_execute(image, ref, snapshot, true, &ops, &r));
  assert(erases == 1 && r.write_started);
  init();
  stop_after = 257;
  assert(!stock_execute(image, ref, snapshot, true, &ops, &r));
  assert(erases == 1);
  init();
  damage_tail = true;
  assert(!stock_execute(image, ref, snapshot, true, &ops, &r));
  assert(erases == 3 && r.address == STOCK_IMAGE_SIZE);
  init();
  assert(stock_execute(image, ref, snapshot, true, &ops, &r));
  assert(erases == 3 && writes == 24 && addresses[0] == 0x92000 && addresses[1] == 0x10000 &&
         addresses[2] == 0x4000);
  assert(!memcmp(flash, image, sizeof image));
  assert(!memcmp(flash + STOCK_IMAGE_SIZE, snapshot + STOCK_IMAGE_SIZE,
                 STOCK_FLASH_SIZE - STOCK_IMAGE_SIZE));
  assert(!stock_guard_sector(0x4000, image + 0x4000));
  assert(uboot::writeSector(0x4000, image + 0x4000) != nullptr);
  init();
  memset(image + STOCK_WRITE_MIN, 0x86, STOCK_IMAGE_SIZE - STOCK_WRITE_MIN);
  assert(stock_execute(image, ref, snapshot, true, &ops, &r));
  assert(erases == 143 && writes == 143 * 8 && addresses[142] == STOCK_WRITE_MIN);
  // Unchanged firmware produces zero erase/write calls, while still reading it.
  init();
  memcpy(image, snapshot, sizeof image);
  assert(stock_execute(image, ref, snapshot, true, &ops, &r));
  assert(!erases && !writes);
  init();
  assert(!stock_plan(image, sizeof image - 1, ref, sizeof ref, snapshot, sizeof snapshot, &r));
  puts(
      "PASS: stock dry run, identity/hash gate callback, protected/reference drift, fresh "
      "comparison, partial failures, readback, one-sector guard, write order, data preservation");
}
