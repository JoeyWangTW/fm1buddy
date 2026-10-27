// Recording the FM-1's USB audio (hal.h audioIn) to WAV files on the SD card, /recordings/take-N.wav, at its own rate
// (16-bit, as it sends it). The writing runs on a thread of its own: the card can take its time, the audio waits
// in a buffer meanwhile (about 6 s on the board)
#pragma once

#include <stdint.h>

#include <string>

#define REC_DIR "/recordings"

namespace record {
// a new take: false (and why) if there's no USB audio, no SD card, or it can't make the file
bool start(std::string& why);
void stop();  // and the file is finished (its sizes written)
bool recording();
std::string file();  // the take's name ("take-3.wav")
uint32_t ms();       // how long it is so far (or the last one was)
// what went wrong once it was going (the card full, ...), "" if nothing; cleared by start
std::string problem();
}  // namespace record
