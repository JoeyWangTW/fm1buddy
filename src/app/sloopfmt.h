// SLOOP's formats, nothing to do with MIDI (so they're tested on the computer: test/sloopfmt_test.cpp): its 7-bit
// packing and numbers, the CRC-32 and base64 of its backup files, and its user sample slots made from .wav files, a
// port of its tools/sampleio.py (read_any_wav, resample, to_int16, ima_encode, user_slot) that gives the same bytes
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#define SLOT_SIZE 0x14000            // a user sample slot: 80 KiB
#define SLOT_DATA_OFF 512            // its ADPCM data, after the header
#define SLOT_HDR_LEN (32 + 16 * 28)  // the header SMP_END takes: 480 bytes
#define SLOT_MAX_DATA (SLOT_SIZE - SLOT_DATA_OFF)
#define SLOT_RATE 22050  // what the samples are made into
#define SLOT_ZONES 16

namespace sloopfmt {
// groups of up to 7 bytes, each after a byte of their top bits
void pack7(const uint8_t* in, size_t n, std::vector<uint8_t>& out);
std::vector<uint8_t> unpack7(const uint8_t* in, size_t n);
// numbers as 5 x 7 bits, low first
void u35(uint32_t v, std::vector<uint8_t>& out);
uint32_t u35(const uint8_t* p);
uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0);  // zlib's
std::string base64(const uint8_t* p, size_t n);
bool unbase64(const std::string& s, std::vector<uint8_t>& out);

// a .wav (PCM 8/16/24/32 bit, or float), its channels mixed to one: false (and why) if it can't be read
bool readWav(const uint8_t* d, size_t n, uint32_t& rate, std::vector<double>& out, std::string& why);
std::vector<double> resample(const std::vector<double>& x, uint32_t from, uint32_t to);
std::vector<int16_t> toInt16(const std::vector<double>& x);  // its peak at 30000
// a note in a file name ("bass_C2.wav", "F#3", "Bb2"), C4 = 60; or dflt
int noteFromName(const std::string& name, int dflt = 60);

struct Zone {
  std::vector<int16_t> samples;  // at SLOT_RATE
  int root;
};
// a slot from its zones (each reaches halfway to its neighbours): the header (SLOT_HDR_LEN bytes) and the ADPCM data;
// false (and why) if it doesn't fit
bool userSlot(const std::string& name, std::vector<Zone> zones, std::vector<uint8_t>& header, std::vector<uint8_t>& data,
              std::string& why);
}  // namespace sloopfmt
