// SLOOP's formats (sloopfmt.h): the packing, the numbers, the CRC and base64, and a user sample slot made from .wav
// files, which must be the bytes SLOOP's own tools/sampleio.py makes from them (its CRCs are below). The .wav files
// are made here with integer sums only, so they're the same on every computer: build/sloop-test/*.wav
#include "sloopfmt.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <string>
#include <vector>

using namespace sloopfmt;

static int fails;

#define CHECK(c)                                     \
  do {                                               \
    if (!(c)) {                                      \
      printf("%s:%d: %s\n", __FILE__, __LINE__, #c); \
      fails++;                                       \
    }                                                \
  } while (0)

static uint32_t lcg = 12345;
static int noise() {
  lcg = lcg * 1103515245u + 12345u;
  return (int)(lcg >> 16 & 0x7FFF) - 16384;
}

// a .wav: rate, channels, bits (8/16/24 PCM, or 32: float), frames of sample(frame, channel) in -32768..32767
template <typename F>
static std::vector<uint8_t> wav(uint32_t rate, int ch, int bits, int frames, F sample) {
  int bps = bits / 8;
  uint32_t data = (uint32_t)(frames * ch * bps);
  std::vector<uint8_t> w(44 + data);
  auto p32 = [&](int at, uint32_t v) {
    for (int i = 0; i < 4; i++) w[at + i] = (uint8_t)(v >> (8 * i));
  };
  auto p16 = [&](int at, uint32_t v) { w[at] = (uint8_t)v, w[at + 1] = (uint8_t)(v >> 8); };
  memcpy(&w[0], "RIFF", 4);
  p32(4, 36 + data);
  memcpy(&w[8], "WAVEfmt ", 8);
  p32(16, 16);
  p16(20, bits == 32 ? 3 : 1);
  p16(22, ch);
  p32(24, rate);
  p32(28, rate * ch * bps);
  p16(32, ch * bps);
  p16(34, bits);
  memcpy(&w[36], "data", 4);
  p32(40, data);
  uint8_t* o = &w[44];
  for (int f = 0; f < frames; f++) {
    for (int c = 0; c < ch; c++, o += bps) {
      int v = sample(f, c);
      if (bits == 8) {
        o[0] = (uint8_t)((v >> 8) + 128);
      } else if (bits == 16) {
        o[0] = (uint8_t)v, o[1] = (uint8_t)(v >> 8);
      } else if (bits == 24) {
        int32_t x = v * 256 + (f & 0xFF);
        o[0] = (uint8_t)x, o[1] = (uint8_t)(x >> 8), o[2] = (uint8_t)(x >> 16);
      } else {
        float x = (float)v / 32768.0f;
        memcpy(o, &x, 4);
      }
    }
  }
  return w;
}

// a triangle wave of period per samples, at amplitude amp
static int tri(int f, int per, int amp) {
  int k = f % per, h = per / 2;
  return (k < h ? k * 2 * amp / h - amp : amp - (k - h) * 2 * amp / h);
}

int main() {
  // packing and numbers
  std::vector<uint8_t> raw, packed;
  for (int i = 0; i < 300; i++) {
    raw.push_back((uint8_t)(i * 37 + 11));
  }
  pack7(raw.data(), raw.size(), packed);
  bool seven = true;
  for (uint8_t b : packed) {
    seven = seven && b < 0x80;
  }
  CHECK(seven && packed.size() == 300 + (300 + 6) / 7);
  CHECK(unpack7(packed.data(), packed.size()) == raw);
  std::vector<uint8_t> n;
  u35(0xCAFEBABE, n);
  CHECK(n.size() == 5 && u35(n.data()) == 0xCAFEBABE);
  CHECK(crc32((const uint8_t*)"123456789", 9) == 0xCBF43926);
  CHECK(base64((const uint8_t*)"hello", 5) == "aGVsbG8=");
  std::vector<uint8_t> back;
  CHECK(unbase64("aGVsbG8=", back) && std::string(back.begin(), back.end()) == "hello");
  CHECK(unbase64(base64(raw.data(), raw.size()), back) && back == raw);
  CHECK(noteFromName("bass_C2") == 36 && noteFromName("lead_A3") == 57 && noteFromName("pad-F#4") == 66);
  CHECK(noteFromName("Bb2 pad") == 46 && noteFromName("hit") == 60 && noteFromName("ABC12") == 60);

  // a slot from four .wav files: 44.1 kHz stereo 16-bit, 48 kHz mono 24-bit, 16 kHz stereo 8-bit, 96 kHz float
  mkdir("build", 0777);
  mkdir("build/sloop-test", 0777);
  struct W {
    const char* name;
    std::vector<uint8_t> data;
  } files[] = {
      {"bass_C2.wav", wav(44100, 2, 16, 26460, [](int f, int c) { return c ? tri(f, 674, 20000) : (f * 97 % 674) * 50 - 16000; })},
      {"lead_A3.wav", wav(48000, 1, 24, 19200, [](int f, int) { return tri(f, 218, 26000); })},
      {"pad-F#4.wav", wav(16000, 2, 8, 4800, [](int f, int c) { return tri(f, 43 + c, 18000); })},
      {"hit.wav", wav(96000, 1, 32, 19200, [](int f, int) { return f < 19200 ? noise() * (19200 - f) / 19200 : 0; })},
  };
  std::vector<Zone> zones;
  for (W& w : files) {
    std::string path = std::string("build/sloop-test/") + w.name;
    FILE* fp = fopen(path.c_str(), "wb");
    fwrite(w.data.data(), 1, w.data.size(), fp);
    fclose(fp);
    uint32_t rate;
    std::vector<double> x;
    std::string why;
    CHECK(readWav(w.data.data(), w.data.size(), rate, x, why));
    zones.push_back({toInt16(resample(x, rate, SLOT_RATE)), noteFromName(w.name)});
  }
  std::vector<uint8_t> hdr, data;
  std::string why;
  CHECK(userSlot("my kit", zones, hdr, data, why));
  uint32_t ch = crc32(hdr.data(), hdr.size()), cd = crc32(data.data(), data.size());
  printf("slot: header %u bytes crc %08X, data %u bytes crc %08X\n", (unsigned)hdr.size(), (unsigned)ch,
         (unsigned)data.size(), (unsigned)cd);
  // what SLOOP's tools/sampleio.py steps (read_any_wav, resample, to_int16, user_slot) make from the same files
  CHECK(hdr.size() == SLOT_HDR_LEN && data.size() == 16537 && ch == 0xA4EC839E && cd == 0xFFA2D177);

  // too long for a slot
  std::vector<Zone> big = {{std::vector<int16_t>(200000, 1000), 60}};
  CHECK(!userSlot("big", big, hdr, data, why) && !why.empty());

  printf(fails ? "sloopfmt: %d failed\n" : "sloopfmt: ok\n", fails);
  return fails != 0;
}
