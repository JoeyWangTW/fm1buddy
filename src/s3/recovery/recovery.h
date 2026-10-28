// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#if defined(FM1BUDDY_RECOVERY_IDF) && !defined(FM1BUDDY_RECOVERY_READ_ONLY)
#error "The experimental IDF recovery backend is read-only; write integration is not validated"
#endif
#include <stdint.h>
namespace recovery {
bool prepare();
bool startHost();
bool waitHost(unsigned ms);
bool present(uint16_t vid, uint16_t pid);
bool seen(uint16_t vid, uint16_t pid);
bool command(const uint8_t* c, uint8_t n, bool in, uint8_t* data, uint32_t len, uint32_t* got);
void stop();  // refuse further commands until reset; no automatic restart
}  // namespace recovery
