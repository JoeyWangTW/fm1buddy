// Pretend USB devices for the simulator: an FM-1 that goes through the whole update (its loader and all), and a
// keyboard that plays now and then. With faults, the FM-1 and its USB misbehave (fake.cpp says how). With fomajam, the
// FM-1 runs fomajam: it shares its song (sounds.h setups)
#pragma once

namespace fake {
void begin(bool fm1, bool keys, bool faults, bool fomajam = false);
// the FM-1 sends a DX7 bank (as from its menu)
void dump();
// fomajam: start or stop playing (it won't load a song while it plays)
void play();
}  // namespace fake
