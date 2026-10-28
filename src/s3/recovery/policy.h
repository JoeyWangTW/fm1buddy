// SPDX-License-Identifier: MIT
// From the Hosyond S3 Transporter port, based on FM-1-transporter (see LICENSE).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
static inline uint32_t be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
// Command boundary: no flash erase/write and no arbitrary RAM execution.
static inline bool command_allowed(const uint8_t *c, size_t n, bool in, size_t len) {
  if (!c || n < 6 || n > 16)
    return false;
  if (c[0] == 0x12)
    return n == 6 && in && len == 36 && c[4] == 36;
  if (n != 16)
    return false;
  uint16_t cmd = (uint16_t)c[0] << 8 | c[1];
  if (cmd == 0xFB06) {
    uint32_t a = be32(c + 2);
    unsigned count = (unsigned)c[6] << 8 | c[7];
    return !in && len > 0 && len <= 512 && count == len && a >= 0x1C02000u &&
           a <= 0x1C02000u + 24064u - len;
  }
  if (cmd == 0xFB08)
    return in && len == 16 && be32(c + 2) == 0x1C02000u && c[6] == 0 && c[7] == 1;
  if (cmd == 0xFD05) {
    unsigned count = (unsigned)c[6] << 8 | c[7];
    return in && len > 0 && len <= 4096 && count == len && be32(c + 2) <= 0x100000u - len;
  }
  if (cmd == 0xFC09)
    return in && len == 16 && be32(c + 2) == 0x00AC6900u;
  if (cmd == 0xFC0C)
    return in && len == 16 && be32(c + 2) == 1;
  return (cmd == 0xFC0A || cmd == 0xFC14) && in && len == 16;
}
// Portable waveform specification, 1 MHz: 4 us setup, then 8 us low / 12 us high;
// final high is 10 us. D- is stable across each complete clock bit.
typedef struct {
  uint16_t low_us, high_us;
  uint8_t data;
} KeyBit;
static inline void key_bits(KeyBit out[16]) {
  for (unsigned i = 0; i < 16; i++)
    out[i] = (KeyBit){8, (uint16_t)(i == 15 ? 10 : 12), (uint8_t)((0x16EFu >> (15 - i)) & 1)};
}

static inline unsigned usb_in_size(unsigned requested, unsigned mps) {
  if (!requested || requested > 4096 || !mps || mps > 64)
    return 0;
  unsigned n = (requested + mps - 1) / mps * mps;
  return n <= 4096 ? n : 0;
}
static inline uint32_t le32_value(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline bool valid_csw(const uint8_t *p, unsigned len, uint32_t tag) {
  return p && len == 13 && le32_value(p) == 0x53425355 && le32_value(p + 4) == tag &&
         le32_value(p + 8) == 0 && p[12] == 0;
}

// Actual upstream M0 hardware trace: SCSI vendor WL82, product UBOOT1.00,
// revision 1.00. WL80UBOOT1.00 is the USB descriptor string, not INQUIRY.
static inline bool rom_inquiry(const uint8_t *p, unsigned len) {
  static const char vendor[] = "WL82", product[] = "UBOOT1.00", revision[] = "1.00";
  if (!p || len != 36)
    return false;
  for (unsigned i = 0; i < 4; i++)
    if (p[8 + i] != (uint8_t)vendor[i] || p[32 + i] != (uint8_t)revision[i])
      return false;
  for (unsigned i = 0; i < 9; i++)
    if (p[16 + i] != (uint8_t)product[i])
      return false;
  for (unsigned i = 12; i < 16; i++)
    if (p[i] != ' ' && p[i] != 0)
      return false;
  for (unsigned i = 25; i < 32; i++)
    if (p[i] != ' ' && p[i] != 0)
      return false;
  return true;
}
