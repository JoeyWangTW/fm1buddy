#include "songmidi.h"

#include <math.h>
#include <stdio.h>
#include <sys/stat.h>

#include <algorithm>

#define PPQ 96  // a 16th: 24 ticks
#define VERSION 9
#define SIZE 11914
#define TRACKS 9
#define STEPS 16
#define PATTERNS 40
#define TRACK_AT 12  // song_t: track[] (40 bytes each), then 8 more bytes, then pat[] (288 bytes each)
#define TRACK_SIZE 40
#define PAT_AT 382
#define STEP_ON 1
#define STEP_ACC 2
#define STEP_SLIDE 4
#define GATE_TIE 101
static const int RATCHET[8] = {1, 2, 3, 4, 6, 8, 8, 8};
// each drum voice's General MIDI note (BD BD2 SD SD2 RS CP LT MT HT CH OH CY RD CB CB2 TB LC MC HC TI MA CL)
static const uint8_t GM[22] = {36, 35, 38, 40, 37, 39, 41, 45, 48, 42, 46, 49, 51, 56, 68, 54, 64, 61, 60, 65, 69, 75};

static uint8_t val(const std::vector<uint8_t>& b, int p, int t, int s) {
  return b[PAT_AT + p * 288 + t * STEPS + s];
}
static uint8_t flag(const std::vector<uint8_t>& b, int p, int t, int s) {
  return b[PAT_AT + p * 288 + 144 + t * STEPS + s];
}

struct Event {
  uint32_t at;
  int order;  // at the same time: offs first
  std::vector<uint8_t> data;
};

static void vlq(std::vector<uint8_t>& o, uint32_t v) {
  uint8_t b[5];
  int n = 0;
  b[n++] = v & 0x7F;
  while (v >>= 7) {
    b[n++] = (v & 0x7F) | 0x80;
  }
  while (n) {
    o.push_back(b[--n]);
  }
}

static void chunk(std::vector<uint8_t>& o, const char* id, const std::vector<uint8_t>& d) {
  o.insert(o.end(), id, id + 4);
  uint32_t n = d.size();
  o.push_back(n >> 24), o.push_back(n >> 16), o.push_back(n >> 8), o.push_back(n);
  o.insert(o.end(), d.begin(), d.end());
}

static void track(std::vector<uint8_t>& o, std::vector<Event>& ev) {
  std::stable_sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) { return a.at != b.at ? a.at < b.at : a.order < b.order; });
  std::vector<uint8_t> d;
  uint32_t now = 0;
  for (auto& e : ev) {
    vlq(d, e.at - now);
    d.insert(d.end(), e.data.begin(), e.data.end());
    now = e.at;
  }
  d.insert(d.end(), {0, 0xFF, 0x2F, 0});
  chunk(o, "MTrk", d);
}

static bool isDrums(const std::vector<uint8_t>& b, int t) {
  if (b[TRACK_AT + t * TRACK_SIZE + 1] > 21) {
    return false;
  }
  for (int p = 0; p < PATTERNS; p++) {
    for (int s = 0; s < STEPS; s++) {
      if ((flag(b, p, t, s) & STEP_ON) && val(b, p, t, s) > 21) {
        return false;
      }
    }
  }
  return true;
}

bool songmidi::pattern(const std::vector<uint8_t>& b, int p, std::vector<uint8_t>& mid) {
  if (b.size() != SIZE || b[0] != VERSION || p < 0 || p >= PATTERNS) {
    return false;
  }
  int length = std::max(1, std::min(STEPS, (int)b[3])), swing = b[2], mute = b[8] | b[9] << 8;
  // when step s starts, and how long it is (swing: the odd ones late)
  double even = lround(2.0 * 24 * (200 + swing) / 400);
  auto start = [&](int s) { return (s / 2) * 48 + ((s & 1) ? even : 0); };
  auto len = [&](int s) { return (s & 1) ? 48 - even : even; };
  uint32_t end = start(length - 1) + len(length - 1);
  std::vector<uint8_t> tracks;
  int count = 0;
  for (int t = 0; t < TRACKS; t++) {
    if (mute >> t & 1) {
      continue;
    }
    bool drums = isDrums(b, t);
    uint8_t ch = drums ? 9 : t, gate = b[TRACK_AT + t * TRACK_SIZE + 30];
    std::vector<Event> ev;
    auto note = [&](double at, uint8_t n, uint8_t vel, double dur) {
      uint32_t a = lround(at), d = std::max<long>(1, lround(dur));
      ev.push_back({a, 1, {(uint8_t)(0x90 | ch), n, vel}});
      ev.push_back({a + d, 0, {(uint8_t)(0x80 | ch), n, 0}});
    };
    for (int s = 0; s < length; s++) {
      uint8_t f = flag(b, p, t, s), v = val(b, p, t, s);
      if (!(f & STEP_ON)) {
        continue;
      }
      uint8_t n = drums ? GM[v] : v & 127, vel = f & STEP_ACC ? 127 : 100;
      int r = RATCHET[(f & 0x38) >> 3];
      if (r > 1) {
        double every = len(s) / r;
        for (int k = 0; k < r; k++) {
          note(lround(start(s) + k * every), n, vel, lround(every / 2));
        }
        continue;
      }
      double dur;
      if (drums) {
        dur = len(s) / 2;
      } else if (f & STEP_SLIDE) {
        dur = len(s) + 2;  // into the next: legato
      } else if (gate >= GATE_TIE) {
        int next = s + 1;
        while (next < length && !(flag(b, p, t, next) & STEP_ON)) {
          next++;
        }
        dur = (next < length ? start(next) : end) - start(s);
      } else {
        dur = len(s) * std::max<int>(1, gate) / 100.0;
      }
      note(start(s), n, vel, lround(dur));
    }
    if (ev.empty()) {
      continue;
    }
    char name[12];
    int k = snprintf(name, sizeof name, "Track %d", t + 1);
    std::vector<uint8_t> meta = {0xFF, 0x03, (uint8_t)k};
    meta.insert(meta.end(), name, name + k);
    ev.push_back({0, 0, meta});
    track(tracks, ev);
    count++;
  }
  if (!count) {
    return false;
  }
  uint32_t us = lround(60000000.0 / std::max<int>(1, b[1]));
  std::vector<Event> head = {{0, 0, {0xFF, 0x51, 3, (uint8_t)(us >> 16), (uint8_t)(us >> 8), (uint8_t)us}},
                             {0, 0, {0xFF, 0x58, 4, 4, 2, 24, 8}},
                             {end, 0, {0xFF, 0x01, 0}}};  // (the pattern's length, for a loop)
  mid.clear();
  chunk(mid, "MThd", {0, 1, 0, (uint8_t)(count + 1), 0, PPQ});
  track(mid, head);
  mid.insert(mid.end(), tracks.begin(), tracks.end());
  return true;
}

int songmidi::write(const std::vector<uint8_t>& song, const std::string& dir) {
  if (song.size() != SIZE || song[0] != VERSION) {
    return -1;
  }
  mkdir(dir.c_str(), 0777);
  int n = 0;
  std::vector<uint8_t> mid;
  for (int p = 0; p < PATTERNS; p++) {
    if (!pattern(song, p, mid)) {
      continue;
    }
    char name[24];
    snprintf(name, sizeof name, "/pattern-%02d.mid", p + 1);
    FILE* f = fopen((dir + name).c_str(), "wb");
    bool ok = f && fwrite(mid.data(), 1, mid.size(), f) == mid.size();
    ok &= f && !fclose(f);
    if (!ok) {
      return -1;
    }
    n++;
  }
  return n;
}
