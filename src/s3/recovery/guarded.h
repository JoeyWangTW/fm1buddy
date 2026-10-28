// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
namespace guarded {
float batteryVolts();
bool powerReady(std::string& error);
bool preflight(const std::string& operation, std::string& error);
// Called by the recovery worker only. A failed write holds this worker and USB
// session; it never returns to a restart/retry UI after an ambiguous mutation.
bool run(const std::string& operation);
}  // namespace guarded
