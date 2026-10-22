// A fomajam song (its src/groove.h, version 9: what it shares as its song) as Standard MIDI Files, one for each pattern
// with steps in it, for a DAW or another sequencer. fomajam's page (web/fm1song.js) makes the same files.
//
// Each track with steps is a MIDI track: drum tracks on channel 10 with General MIDI drum notes (as the firmware takes
// them in), the others on channels 1-9 (track 1 on 1) with their notes. A drum track is one whose steps (and its own
// voice) are all drum voices (0-21): a synth's notes never are that low in practice. Accents are velocity 127 (else
// 100), ratchets are repeated notes, slides and TIE hold a note into the next, GATE is how long the others are held.
// The tempo and swing are the song's; muted tracks are left out.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

namespace songmidi {
// pattern p (0-39) as a .mid; false if the song isn't one this knows, or the pattern has no steps
bool pattern(const std::vector<uint8_t>& song, int p, std::vector<uint8_t>& mid);
// every pattern with steps into dir, as pattern-01.mid ...; how many, or -1 (not a song this knows, or not written)
int write(const std::vector<uint8_t>& song, const std::string& dir);
}  // namespace songmidi
