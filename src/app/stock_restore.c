#ifdef FM1BUDDY_STOCK_RESTORE
// SPDX-License-Identifier: MIT
// Fixed-stock restore policy. Wire protocol follows FM-1-transporter (MIT).
#include "stock_restore.h"
static unsigned be32(const unsigned char *p) {
  return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
}
static unsigned short jl_crc16(const unsigned char *p, unsigned n, unsigned short crc) {
  while (n--) {
    crc ^= (unsigned short)*p++ << 8;
    for (int b = 0; b < 8; b++)
      crc = crc & 0x8000 ? (unsigned short)((crc << 1) ^ 0x1021) : (unsigned short)(crc << 1);
  }
  return crc;
}
#include <string.h>
static const uint8_t *window_data;
static uint32_t window_address;
static unsigned window_step;
static void close_window(void) {
  window_data = NULL;
  window_step = 0;
}
bool stock_guard_sector(uint32_t a, const uint8_t *data) {
  return window_data && a == window_address && data == window_data && window_step == 0;
}
bool stock_guard_command(const uint8_t *c, size_t n, bool in, const void *data, size_t len) {
  if (!window_data || !c || n != 16 || window_address < STOCK_WRITE_MIN ||
      window_address >= STOCK_IMAGE_SIZE || window_address % STOCK_SECTOR)
    return false;
  unsigned op = (unsigned)c[0] << 8 | c[1];
  if (op == 0xFB01 && window_step == 0 && in && len == 16 && be32(c + 2) == window_address) {
    for (unsigned i = 6; i < 16; i++)
      if (c[i] != 0xFF)
        return false;
    window_step = 1;
    return true;
  }
  if (op != 0xFB04 || in || len != 512 || !data || window_step < 1 || window_step > 8 ||
      be32(c + 2) != window_address + (window_step - 1) * 512 || c[6] != 2 || c[7] || c[8])
    return false;
  for (unsigned i = 11; i < 16; i++)
    if (c[i] != 0xFF)
      return false;
  if (memcmp(data, window_data + (window_step - 1) * 512, 512) ||
      jl_crc16(data, 512, 0) != ((unsigned)c[9] | (unsigned)c[10] << 8))
    return false;
  ++window_step;
  return true;
}
static bool fail(StockResult *r, const char *e, uint32_t a) {
  r->error = e;
  r->address = a;
  return false;
}
bool stock_plan(const uint8_t *image, size_t ni, const uint8_t *ref, size_t nr, const uint8_t *now,
                size_t nn, StockResult *r) {
  memset(r, 0, sizeof *r);
  if (!image || !ref || !now || ni != STOCK_IMAGE_SIZE || nr != STOCK_FLASH_SIZE || nn != nr)
    return fail(r, "Invalid image/reference size", 0);
  for (uint32_t a = 0; a < STOCK_FLASH_SIZE; a += STOCK_SECTOR) {
    if (a < STOCK_WRITE_MIN && memcmp(image + a, now + a, STOCK_SECTOR))
      return fail(r, "Protected boot area differs", a);
    if (a < STOCK_IMAGE_SIZE && memcmp(ref + a, now + a, STOCK_SECTOR))
      return fail(r, "Flash differs from reference", a);
    if (a < STOCK_IMAGE_SIZE && memcmp(image + a, now + a, STOCK_SECTOR))
      ++r->sectors;
    if (a >= STOCK_IMAGE_SIZE && memcmp(ref + a, now + a, STOCK_SECTOR))
      ++r->data_changed;
  }
  return true;
}
bool stock_execute(const uint8_t *image, const uint8_t *ref, const uint8_t *now, bool write,
                   const StockOps *o, StockResult *r) {
  close_window();
  memset(r, 0, sizeof *r);
  if (!o || !o->authenticate || !o->read || !o->may_continue || !o->write || !image || !ref ||
      !now || !o->authenticate(o->ctx, image, ref))
    return fail(r, "Package/reference hash failed", 0);
  if (!stock_plan(image, STOCK_IMAGE_SIZE, ref, STOCK_FLASH_SIZE, now, STOCK_FLASH_SIZE, r))
    return false;
  _Alignas(4) uint8_t check[STOCK_SECTOR];
  for (uint32_t a = 0; a < STOCK_FLASH_SIZE; a += STOCK_SECTOR) {
    if (!o->may_continue(o->ctx))
      return fail(r, "Cancelled or low battery", a);
    if (!o->read(o->ctx, a, check, sizeof check))
      return fail(r, "Fresh comparison read failed", a);
    if (memcmp(check, now + a, sizeof check))
      return fail(r, "Flash changed after backup", a);
  }
  if (!write)
    return true;
  unsigned done = 0;
  // Highest sector first; replace the application entry region last.
  for (uint32_t end = STOCK_IMAGE_SIZE; end > STOCK_WRITE_MIN; end -= STOCK_SECTOR) {
    uint32_t a = end - STOCK_SECTOR;
    if (!memcmp(image + a, now + a, STOCK_SECTOR))
      continue;
    if (!o->may_continue(o->ctx))
      return fail(r, "Cancelled or low battery", a);
    window_data = image + a;
    window_address = a;
    window_step = 0;
    r->write_started = true;
    bool ok = o->write(o->ctx, a, image + a) && window_step == 9;
    close_window();  // No retry, including an ambiguous timeout after erase/write.
    if (!ok)
      return fail(r, "Sector erase/write failed", a);
    if (!o->read(o->ctx, a, check, sizeof check) || memcmp(check, image + a, sizeof check))
      return fail(r, "Sector readback failed", a);
    if (o->progress)
      o->progress(o->ctx, a, ++done, r->sectors);
  }
  // Include protected bytes and device data in final verification.
  for (uint32_t a = 0; a < STOCK_FLASH_SIZE; a += STOCK_SECTOR) {
    const uint8_t *expected = a < STOCK_IMAGE_SIZE ? image + a : now + a;
    if (!o->read(o->ctx, a, check, sizeof check) || memcmp(check, expected, sizeof check))
      return fail(r, "Final full-flash verify failed", a);
  }
  return true;
}

#endif
