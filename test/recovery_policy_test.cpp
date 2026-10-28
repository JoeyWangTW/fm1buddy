// Read-only USB boundary and actual fm1buddy reader, including truncated responses.
#include <cassert>
#include <cstdio>
#include <cstring>

#include "../src/s3/recovery/policy.h"
#include "hal.h"
#include "uboot.h"

static unsigned calls;
static bool truncated;
namespace hal {
void log(const char*, ...) {}
bool storageCommand(const uint8_t* c, uint8_t n, bool in, uint8_t* data, uint32_t len, uint32_t* got, uint32_t) {
  assert(command_allowed(c, n, in, len));
  assert(c[0] == 0xFD && c[1] == 5);
  ++calls;
  for (uint32_t i = 0; i < len; ++i) data[i] = (uint8_t)(be32(c + 2) + i);
  if (got)
    *got = len - (truncated ? 1 : 0);
  return true;
}
}  // namespace hal
int main() {
  KeyBit bits[16];
  key_bits(bits);
  unsigned word = 0, duration = 4;
  for (unsigned i = 0; i < 16; ++i) {
    word = word * 2 + bits[i].data;
    assert(bits[i].low_us == 8 && bits[i].high_us == (i == 15 ? 10 : 12));
    duration += bits[i].low_us + bits[i].high_us;
  }
  assert(word == 0x16EF && duration == 322);
  assert(usb_in_size(13, 64) == 64 && usb_in_size(36, 64) == 64);
  assert(usb_in_size(4096, 64) == 4096 && !usb_in_size(4097, 64));
  uint8_t csw[13] = {'U', 'S', 'B', 'S', 7};
  assert(valid_csw(csw, 13, 7));
  assert(!valid_csw(csw, 12, 7) && !valid_csw(csw, 13, 8));
  csw[8] = 1;
  assert(!valid_csw(csw, 13, 7));
  csw[8] = 0;
  csw[12] = 1;
  assert(!valid_csw(csw, 13, 7));
  uint8_t inquiry[36] = {};
  memcpy(inquiry + 8, "WL82    UBOOT1.00       1.00", 28);
  assert(rom_inquiry(inquiry, sizeof inquiry));
  inquiry[8] = 'X';
  assert(!rom_inquiry(inquiry, sizeof inquiry));
  // Erase, flash write, arbitrary RAM reads/jumps and every other opcode are closed.
  uint8_t c[16];
  for (unsigned op = 0; op < 65536; ++op) {
    memset(c, 0xFF, sizeof c);
    c[0] = op >> 8;
    c[1] = op;
    if (command_allowed(c, 16, true, 16))
      assert(op == 0xFC0A || op == 0xFC14);
    assert(!command_allowed(c, 16, false, 16));
  }
  uint8_t ram[16] = {0xFB, 6, 1, 0xC0, 0x20, 0, 2, 0};
  assert(command_allowed(ram, 16, false, 512));
  ram[2] = 0;
  assert(!command_allowed(ram, 16, false, 512));
  uboot::Chip chip{FM1_KEY, FM1_FLASH, 1048576, 4096};
  std::vector<uint8_t> out;
  assert(!uboot::read(chip, out, nullptr));
  assert(calls == 256 && out.size() == chip.size);
  for (unsigned i = 0; i < out.size(); ++i) assert(out[i] == (uint8_t)i);
  calls = 0;
  truncated = true;
  assert(uboot::read(chip, out, nullptr) != nullptr && calls == 3);
  puts("recovery: waveform, command boundary, CSW/identity, full reader and short-read rejection passed");
}
