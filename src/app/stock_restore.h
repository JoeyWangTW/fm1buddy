// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define STOCK_IMAGE_SIZE 0x93000u
#define STOCK_FLASH_SIZE 0x100000u
#define STOCK_WRITE_MIN 0x4000u
#define STOCK_SECTOR 0x1000u
#define STOCK_BUNDLE_SIZE (112u + STOCK_IMAGE_SIZE + STOCK_FLASH_SIZE)
#define STOCK_PACKAGE_SHA "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"
#define STOCK_IMAGE_SHA "c1068a403c04a6378a626afb66cc217f72d13f10aa200e983b7a9817cc1dac03"
typedef struct {
  unsigned sectors, data_changed;
  uint32_t address;
  const char *error;
  bool write_started;
} StockResult;
typedef struct {
  void *ctx;
  // Must authenticate the exact pinned image and reference digest before any I/O.
  bool (*authenticate)(void *, const uint8_t *, const uint8_t *);
  bool (*read)(void *, uint32_t, uint8_t *, size_t);
  bool (*write)(void *, uint32_t, const uint8_t *);
  bool (*may_continue)(void *);  // sampled before erasing each sector, never mid-sector
  void (*progress)(void *, uint32_t, unsigned, unsigned);
} StockOps;
bool stock_plan(const uint8_t *image, size_t image_size, const uint8_t *reference,
                size_t reference_size, const uint8_t *current, size_t current_size,
                StockResult *result);
// Caller has already saved and verified a fresh backup. This function re-reads
// it from the target before writing, verifies every sector, then the full flash.
bool stock_execute(const uint8_t *image, const uint8_t *reference, const uint8_t *current,
                   bool write, const StockOps *ops, StockResult *result);
// A transient one-sector command window, opened only inside stock_execute.
// Flash opcodes remain rejected by command_allowed; USB consults this additionally.
bool stock_guard_command(const uint8_t *c, size_t n, bool in, const void *data, size_t len);
bool stock_guard_sector(uint32_t addr, const uint8_t *data);
#ifdef __cplusplus
}
#endif
