#include "sloopfmt.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

namespace sloopfmt {

void pack7(const uint8_t* in, size_t n, std::vector<uint8_t>& out) {
  for (size_t i = 0; i < n; i += 7) {
    size_t g = n - i < 7 ? n - i : 7;
    uint8_t top = 0;
    for (size_t j = 0; j < g; j++) {
      top |= (uint8_t)(((in[i + j] >> 7) & 1) << j);
    }
    out.push_back(top);
    for (size_t j = 0; j < g; j++) {
      out.push_back(in[i + j] & 0x7F);
    }
  }
}

std::vector<uint8_t> unpack7(const uint8_t* in, size_t n) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i < n; i += 8) {
    uint8_t top = in[i];
    for (size_t j = 1; j < 8 && i + j < n; j++) {
      out.push_back((uint8_t)(in[i + j] | ((top >> (j - 1)) & 1) << 7));
    }
  }
  return out;
}

void u35(uint32_t v, std::vector<uint8_t>& out) {
  for (int k = 0; k < 5; k++) {
    out.push_back((uint8_t)((uint64_t)v >> (7 * k) & 0x7F));
  }
}

uint32_t u35(const uint8_t* p) {
  uint64_t v = 0;
  for (int k = 0; k < 5; k++) {
    v |= (uint64_t)(p[k] & 0x7F) << (7 * k);
  }
  return (uint32_t)v;
}

uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc) {
  static uint32_t table[256];
  if (!table[1]) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) {
        c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      }
      table[i] = c;
    }
  }
  crc = ~crc;
  for (size_t i = 0; i < n; i++) {
    crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  }
  return ~crc;
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(const uint8_t* p, size_t n) {
  std::string s;
  s.reserve((n + 2) / 3 * 4);
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
    s += B64[v >> 18 & 63];
    s += B64[v >> 12 & 63];
    s += i + 1 < n ? B64[v >> 6 & 63] : '=';
    s += i + 2 < n ? B64[v & 63] : '=';
  }
  return s;
}

bool unbase64(const std::string& s, std::vector<uint8_t>& out) {
  out.clear();
  out.reserve(s.size() / 4 * 3);
  uint32_t v = 0;
  int bits = 0;
  for (char c : s) {
    if (c == '=' || isspace((unsigned char)c)) {
      continue;
    }
    const char* at = strchr(B64, c);
    if (!at || !c) {
      return false;
    }
    v = v << 6 | (uint32_t)(at - B64);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((uint8_t)(v >> bits));
    }
  }
  return true;
}

// ------------------------------------------------------------------------------------------- .wav ---
static uint32_t le(const uint8_t* p, int n) {
  uint32_t v = 0;
  for (int i = 0; i < n; i++) {
    v |= (uint32_t)p[i] << (8 * i);
  }
  return v;
}

bool readWav(const uint8_t* d, size_t n, uint32_t& rate, std::vector<double>& out, std::string& why) {
  out.clear();
  if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) {
    why = "not a .wav";
    return false;
  }
  // its chunks: the first of each wins
  size_t fmt = 0, data = 0, dataLen = 0;
  for (size_t i = 12; i + 8 <= n;) {
    uint32_t len = le(d + i + 4, 4);
    if (!memcmp(d + i, "fmt ", 4) && !fmt) {
      fmt = i + 8;
    } else if (!memcmp(d + i, "data", 4) && !data) {
      data = i + 8, dataLen = len;
    }
    i += 8 + (size_t)len + (len & 1);
  }
  if (!fmt || fmt + 16 > n) {
    why = "a .wav without its format";
    return false;
  }
  uint32_t tag = le(d + fmt, 2), nch = le(d + fmt + 2, 2), bits = le(d + fmt + 14, 2);
  rate = le(d + fmt + 4, 4);
  if (tag == 0xFFFE && fmt + 26 <= n) {
    tag = le(d + fmt + 24, 2);
  }
  uint32_t bps = bits / 8;
  if ((tag != 1 && tag != 3) || !bps || !nch || (tag == 3 && bps != 4 && bps != 8) || bps > 8) {
    why = "a .wav it can't read (" + std::to_string(bits) + " bit)";
    return false;
  }
  if (!data) {
    return true;  // (no audio)
  }
  size_t frame = (size_t)bps * nch;
  size_t frames = std::min(dataLen, n - data) / frame;
  out.reserve(frames);
  for (size_t f = 0; f < frames; f++) {
    double acc = 0.0;
    for (uint32_t c = 0; c < nch; c++) {
      const uint8_t* p = d + data + f * frame + c * bps;
      double v;
      if (tag == 3) {
        if (bps == 4) {
          float x;
          memcpy(&x, p, 4);
          v = x;
        } else {
          double x;
          memcpy(&x, p, 8);
          v = x;
        }
      } else if (bps == 2) {
        v = (int16_t)le(p, 2) / 32768.0;
      } else if (bps == 3) {
        v = (int32_t)(le(p, 3) << 8) / 2147483648.0;  // (as sampleio: the low byte padded with 0)
      } else if (bps == 4) {
        v = (int32_t)le(p, 4) / 2147483648.0;
      } else {
        v = ((int)p[0] - 128) / 128.0;
      }
      if (nch == 1) {
        acc = v;
      } else {
        acc += v;
      }
    }
    out.push_back(nch == 1 ? acc : acc / nch);  // (two channels: (0.0 + l + r) / 2, as sampleio)
  }
  return true;
}

std::vector<double> resample(const std::vector<double>& in, uint32_t sr, uint32_t to) {
  if (sr == to) {
    return in;
  }
  std::vector<double> x = in;
  if (sr > to) {
    int k = std::max(1, (int)nearbyint((double)sr / to));  // (half to even, as Python's round)
    if (k == 2 && !x.empty()) {
      std::vector<double> y(x.size());
      for (size_t i = 0; i + 1 < x.size(); i++) {
        y[i] = (x[i] + x[i + 1]) / 2;
      }
      y.back() = x.back() / 2;
      x.swap(y);
    } else if (k > 2) {
      std::vector<double> y(x.size());
      for (size_t i = 0; i < x.size(); i++) {  // left to right, as reduce(add, ...)
        size_t end = std::min(x.size(), i + (size_t)k);
        double s = x[i];
        for (size_t j = i + 1; j < end; j++) {
          s += x[j];
        }
        y[i] = s / k;
      }
      x.swap(y);
    }
  }
  std::vector<double> out;
  double step = (double)sr / to, p = 0.0;
  double last = x.empty() ? 0 : (double)(x.size() - 1);
  while (p < last) {
    size_t i = (size_t)p;
    double f = p - i;
    out.push_back(x[i] * (1 - f) + x[i + 1] * f);
    p += step;
  }
  return out;
}

std::vector<int16_t> toInt16(const std::vector<double>& x) {
  double pk = 1e-9;
  for (double v : x) {
    pk = std::max(pk, fabs(v));
  }
  std::vector<int16_t> out(x.size());
  for (size_t i = 0; i < x.size(); i++) {
    double v = trunc(x[i] / pk * 30000);  // (Python's int(): toward 0)
    out[i] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767
                                                       : v);
  }
  return out;
}

int noteFromName(const std::string& s, int dflt) {
  static const int PC[] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (c < 'A' || c > 'G' || (i && isalpha((unsigned char)s[i - 1]))) {
      continue;
    }
    size_t j = i + 1;
    int acc = 0;
    if (j < s.size() && (s[j] == '#' || s[j] == 'b')) {
      acc = s[j] == '#' ? 1 : -1;
      j++;
    }
    bool neg = j < s.size() && s[j] == '-';
    if (neg) {
      j++;
    }
    if (j >= s.size() || !isdigit((unsigned char)s[j]) || (j + 1 < s.size() && isdigit((unsigned char)s[j + 1]))) {
      continue;
    }
    int oct = (s[j] - '0') * (neg ? -1 : 1);
    int n = (oct + 1) * 12 + PC[c - 'A'] + acc;
    return n < 0 ? 0 : n > 127 ? 127
                               : n;
  }
  return dflt;
}

// ------------------------------------------------------------------------------------------ slots ---
static const int16_t STEPS[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28,
    31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
    544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t ADJ[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

// IMA ADPCM, low nibble first, from predictor 0 and step index 0 (sampleio.ima_encode)
static void ima(const std::vector<int16_t>& x, std::vector<uint8_t>& out) {
  int pred = 0, idx = 0;
  std::vector<uint8_t> nib;
  nib.reserve(x.size() + 1);
  for (int16_t s : x) {
    int step = STEPS[idx], diff = s - pred, code = 0;
    if (diff < 0) {
      code = 8;
      diff = -diff;
    }
    int vd = step >> 3;
    if (diff >= step) {
      code |= 4;
      diff -= step;
      vd += step;
    }
    if (diff >= step >> 1) {
      code |= 2;
      diff -= step >> 1;
      vd += step >> 1;
    }
    if (diff >= step >> 2) {
      code |= 1;
      vd += step >> 2;
    }
    pred = code & 8 ? pred - vd : pred + vd;
    pred = pred > 32767 ? 32767 : pred < -32768 ? -32768
                                                : pred;
    idx += ADJ[code & 7];
    idx = idx < 0 ? 0 : idx > 88 ? 88
                                 : idx;
    nib.push_back((uint8_t)code);
  }
  if (nib.size() & 1) {
    nib.push_back(0);
  }
  for (size_t i = 0; i < nib.size(); i += 2) {
    out.push_back((uint8_t)(nib[i] | nib[i + 1] << 4));
  }
}

static void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    b[at + i] = (uint8_t)(v >> (8 * i));
  }
}

bool userSlot(const std::string& name, std::vector<Zone> zones, std::vector<uint8_t>& hdr, std::vector<uint8_t>& data,
              std::string& why) {
  struct Z {
    uint32_t off, n;
    int root;
  };
  std::vector<Z> zs;
  data.clear();
  for (const Zone& z : zones) {
    zs.push_back({(uint32_t)data.size(), (uint32_t)z.samples.size(), z.root});
    ima(z.samples, data);
  }
  if (data.size() > SLOT_MAX_DATA) {
    char s[128];
    snprintf(s, sizeof s, "Too long: a slot holds %.1f s in all, these are %.1f s.", SLOT_MAX_DATA * 2.0 / SLOT_RATE,
             data.size() * 2.0 / SLOT_RATE);
    why = s;
    return false;
  }
  if (zs.empty() || zs.size() > SLOT_ZONES) {
    why = "1 to 16 files a slot.";
    return false;
  }
  std::stable_sort(zs.begin(), zs.end(), [](const Z& a, const Z& b) { return a.root < b.root; });
  hdr.assign(SLOT_HDR_LEN, 0);
  put32(hdr, 0, 0x504D5346);  // "FSMP"
  hdr[4] = 1;                 // version
  hdr[6] = (uint8_t)zs.size();
  std::string nm;
  for (char c : name) {
    if (c >= 0x20 && c <= 0x7E && nm.size() < 8) {
      nm += (char)toupper((unsigned char)c);
    }
  }
  memcpy(&hdr[8], nm.data(), nm.size());
  put32(hdr, 16, (uint32_t)data.size());
  put32(hdr, 20, crc32(data.data(), data.size()));
  uint32_t rate = (uint32_t)nearbyint((double)SLOT_RATE / 44100 * 65536);
  size_t n = zs.size();
  for (size_t j = 0; j < n; j++) {
    const Z& z = zs[j];
    int lo = j == 0 ? 0 : (zs[j - 1].root + z.root) / 2 + 1;  // halfway to the neighbours (key_split)
    int hi = j == n - 1 ? 127 : (z.root + zs[j + 1].root) / 2;
    size_t at = 32 + j * 28;
    put32(hdr, at, z.off);
    put32(hdr, at + 4, z.n);
    put32(hdr, at + 8, 0);                            // loop start
    put32(hdr, at + 12, z.n ? z.n - 1 : 0xFFFFFFFF);  // loop end
    put32(hdr, at + 16, rate);
    int16_t root16 = (int16_t)(z.root * 16);
    memcpy(&hdr[at + 20], &root16, 2);  // (then the predictor at the loop start, 0, and its step index, 0)
    hdr[at + 25] = (uint8_t)lo;
    hdr[at + 26] = (uint8_t)hi;
  }
  return true;
}
}  // namespace sloopfmt
