// Compile the real recovery writer in diagnostic mode and prove it never reaches USB.
#include "uboot.h"
#include "hal.h"
#include <cassert>
#include <cstring>
#include <cstdio>

static unsigned calls;
namespace hal {
bool storageCommand(const uint8_t*, uint8_t, bool, uint8_t*, uint32_t, uint32_t*, uint32_t) {
  ++calls;
  return true;
}
}
int main() {
  uint8_t sector[SECTOR] = {};
  for (uint32_t address : {0u, WRITE_FROM, WRITE_TO - SECTOR, WRITE_TO, 0xFFFFFFFFu}) {
    const char* error = uboot::writeSector(address, sector);
    assert(error && strstr(error, "disabled"));
  }
  assert(calls == 0);
  puts("diagnostic: flash writes refused; zero USB commands");
}
