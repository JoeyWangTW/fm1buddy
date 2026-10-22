// Pretend USB devices for the simulator: an FM-1 that goes through the whole update (its loader and all), and a
// keyboard that plays now and then. With faults, the FM-1 and its USB misbehave (fake.cpp says how)
#pragma once

namespace fake {
void begin(bool fm1, bool keys, bool faults);
// the FM-1 sends a DX7 bank (as from its menu)
void dump();
}  // namespace fake
