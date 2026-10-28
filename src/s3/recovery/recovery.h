// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#if defined(FM1BUDDY_RECOVERY_READ_ONLY) && defined(FM1BUDDY_STOCK_RESTORE)
#error "Choose read-only diagnostics or guarded stock restoration, not both"
#endif
#if defined(FM1BUDDY_RECOVERY_IDF) && !defined(FM1BUDDY_RECOVERY_READ_ONLY) && !defined(FM1BUDDY_STOCK_RESTORE)
#error "IDF recovery requires read-only mode or the guarded stock restore policy"
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
