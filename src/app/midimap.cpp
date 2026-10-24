#include "midimap.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TYPE_NAMES[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, "note", 0, "cc", "pc", "at", "pb", 0};

static int typeOf(const char* w) {
  for (int k = 0; k < 16; k++) {
    if (TYPE_NAMES[k] && !strcmp(w, TYPE_NAMES[k])) {
      return k;
    }
  }
  return 0;
}

static bool space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// words, lowercase; '...' and "..." keep their spaces
static int words(const char* s, char w[][24], int max) {
  int n = 0;
  while (*s && n < max) {
    while (space(*s)) {
      s++;
    }
    if (!*s) {
      break;
    }
    char q = *s == '"' || *s == '\'' ? *s++ : 0;
    int k = 0;
    while (*s && (q ? *s != q : !space(*s))) {
      if (k < 23) {
        w[n][k++] = (char)tolower((unsigned char)*s);
      }
      s++;
    }
    if (q && *s) {
      s++;
    }
    w[n++][k] = 0;
  }
  return n;
}

static bool number(const char* w, int max, uint8_t& v) {
  if (!*w) {
    return false;
  }
  for (const char* c = w; *c; c++) {
    if (!isdigit((unsigned char)*c)) {
      return false;
    }
  }
  int n = atoi(w);
  if (n > max) {
    return false;
  }
  v = (uint8_t)n;
  return true;
}

// [from|to NAME] [ch N] [TYPE [NUM]]: the words used, -1 on an error
static int side(char w[][24], int n, bool out, MapRule& r, const char** why) {
  int i = 0;
  if (i < n && !strcmp(w[i], out ? "to" : "from")) {
    if (i + 1 >= n) {
      *why = "a name after from/to";
      return -1;
    }
    snprintf(out ? r.to : r.from, sizeof r.from, "%s", w[i + 1]);
    i += 2;
  }
  if (i < n && !strcmp(w[i], "ch")) {
    uint8_t ch;
    if (i + 1 >= n || !number(w[i + 1], 16, ch) || !ch) {
      *why = "ch is 1-16";
      return -1;
    }
    (out ? r.outCh : r.inCh) = ch;
    i += 2;
  }
  if (i < n && typeOf(w[i])) {
    (out ? r.outType : r.inType) = (uint8_t)typeOf(w[i]);
    i++;
    if (i < n && isdigit((unsigned char)w[i][0])) {
      uint8_t v;
      if (!number(w[i], 127, v)) {
        *why = "numbers are 0-127";
        return -1;
      }
      (out ? r.outNum : r.inNum) = v;
      i++;
    }
  }
  return i;
}

bool midimap::parse(const char* line, MapRule& r, const char** why) {
  char buf[160], w[16][24];
  *why = 0;
  snprintf(buf, sizeof buf, "%s", line);
  if (char* hash = strchr(buf, '#')) {
    *hash = 0;
  }
  r = MapRule();
  char* arrow = strstr(buf, "->");
  if (!arrow) {
    *why = words(buf, w, 16) ? "a rule needs ->" : 0;
    return false;
  }
  *arrow = 0;
  int n = words(buf, w, 16);
  int used = side(w, n, false, r, why);
  if (used < 0) {
    return false;
  }
  if (used < n) {
    *why = "something left over before ->";
    return false;
  }
  n = words(arrow + 2, w, 16);
  if (n == 1 && !strcmp(w[0], "drop")) {
    r.outType = MAP_DROP;
    return true;
  }
  used = side(w, n, true, r, why);
  if (used < 0) {
    return false;
  }
  if (used < n) {
    *why = "something left over after ->";
    return false;
  }
  return true;
}

bool midimap::load(const char* path, std::vector<MapRule>& out, std::string& problem) {
  out.clear();
  problem.clear();
  FILE* f = fopen(path, "r");
  if (!f) {
    return false;
  }
  char line[160];
  for (int ln = 1; fgets(line, sizeof line, f); ln++) {
    MapRule r;
    const char* why;
    if (parse(line, r, &why)) {
      if (out.size() < MAP_RULES) {
        out.push_back(r);
      } else if (problem.empty()) {
        problem = "line " + std::to_string(ln) + ": more than " + std::to_string(MAP_RULES) + " rules";
      }
    } else if (why && problem.empty()) {
      problem = "line " + std::to_string(ln) + ": " + why;
    }
  }
  fclose(f);
  return true;
}

// the top of the file, so it explains itself
static const char HELP[] =
    "# fm1buddy MIDI mapping, for MIDI routing: one rule per line. Without rules, everything any device plays goes\n"
    "# to all the others.\n"
    "#\n"
    "#   [from NAME] [ch N] [TYPE [NUM]] -> [to NAME] [ch N] [TYPE [NUM]]\n"
    "#   [from NAME] [ch N] [TYPE [NUM]] -> drop\n"
    "#\n"
    "# TYPE is note, cc, pc (program change), at (channel pressure) or pb (pitch bend). NAME is part of a device's\n"
    "# name, as the MIDI page shows it (any case; \"quote it\" if it has spaces). On the right, what you leave out\n"
    "# stays as it came in; without \"to\", it goes to all the other devices. The first rule that matches a message\n"
    "# decides; a message no rule matches goes on as it is. Clock, start/stop and SysEx always go on as they are.\n"
    "# For example, two controllers playing the FM-1 on channels of their own:\n"
    "#\n"
    "#   from nanokey -> to fm-1 ch 1\n"
    "#   from \"launchkey mini\" -> to fm-1 ch 2\n"
    "#   from nanokontrol2 ch 1 cc 10 -> to fm-1 ch 2 cc 5      CC 10 becomes CC 5 on channel 2\n"
    "#   from launchkey note 36 -> to fm-1 note 38\n"
    "#   cc 1 -> pc                                            the mod wheel picks programs\n"
    "#   pb -> drop                                            no pitch bend from anything\n"
    "#\n"
    "# The rules are read when fm1buddy starts, when MIDI routing is turned on, and with the reload button on the\n"
    "# MIDI page.\n";

bool midimap::writeHelp(const char* path) {
  FILE* f = fopen(path, "w");
  if (!f) {
    return false;
  }
  bool ok = fputs(HELP, f) >= 0;
  return !fclose(f) && ok;
}

bool midimap::matches(const char* part, const char* name) {
  if (!*part) {
    return true;
  }
  char lower[64];
  int k = 0;
  for (; name[k] && k < (int)sizeof lower - 1; k++) {
    lower[k] = (char)tolower((unsigned char)name[k]);
  }
  lower[k] = 0;
  return strstr(lower, part);
}

static uint32_t lenOf(uint8_t status) {
  uint8_t k = status >> 4;
  return k == 0xC || k == 0xD ? 2 : 3;
}

int midimap::apply(const std::vector<MapRule>& rules, const uint32_t* from, const uint32_t* to, int d,
                   const uint8_t* msg, uint8_t* out, uint32_t* outN, uint32_t* toMask) {
  uint8_t kind = msg[0] >> 4, ch = (msg[0] & 15) + 1;
  uint8_t d1 = lenOf(msg[0]) > 1 ? msg[1] : 0, d2 = lenOf(msg[0]) > 2 ? msg[2] : 0;
  bool hasNum = kind == 0x8 || kind == 0x9 || kind == 0xA || kind == 0xB;
  for (size_t i = 0; i < rules.size(); i++) {
    const MapRule& r = rules[i];
    if (!(from[i] >> d & 1)) {
      continue;
    }
    if (r.inCh && r.inCh != ch) {
      continue;
    }
    if (r.inType) {
      if (r.inType == MAP_NOTE ? kind != 0x8 && kind != 0x9 : r.inType != kind) {
        continue;
      }
      // the number: a note's, a CC's, or the program
      if (r.inNum != MAP_ANY && (hasNum || kind == 0xC) && d1 != r.inNum) {
        continue;
      }
    }
    *toMask = to[i];
    // dropped, or "to" a device that isn't there: nowhere, not everywhere
    if (r.outType == MAP_DROP || (r.to[0] && !to[i])) {
      *outN = 0;
      return (int)i;
    }
    // the message's value, and what it becomes
    uint8_t val = hasNum ? d2 : kind == 0xE ? d2
                                            : d1;
    if (kind == 0x8) {
      val = 0;
    }
    uint8_t okind = r.outType ? r.outType : kind;
    uint8_t c = (uint8_t)((r.outCh ? r.outCh : ch) - 1);
    uint8_t num = r.outNum != MAP_ANY ? r.outNum : hasNum ? d1
                                                          : 0;
    if (okind == 0x8 || okind == 0x9) {
      if (r.outNum == MAP_ANY && !hasNum) {
        num = 60;
      }
      // a note off stays one (with its own velocity); anything else is on at its value, off at 0
      if (kind == 0x8 && !r.outType) {
        out[0] = (uint8_t)(0x80 | c), out[1] = num, out[2] = d2;
      } else {
        out[0] = (uint8_t)((val ? 0x90 : 0x80) | c), out[1] = num, out[2] = val;
      }
      *outN = 3;
    } else if (okind == MAP_POLY || okind == MAP_CC) {
      out[0] = (uint8_t)(okind << 4 | c), out[1] = num, out[2] = val;
      *outN = 3;
    } else if (okind == MAP_PC) {
      out[0] = (uint8_t)(0xC0 | c), out[1] = r.outNum != MAP_ANY ? r.outNum : val;
      *outN = 2;
    } else if (okind == MAP_AT) {
      out[0] = (uint8_t)(0xD0 | c), out[1] = val;
      *outN = 2;
    } else {  // MAP_PB
      out[0] = (uint8_t)(0xE0 | c), out[1] = kind == 0xE ? d1 : 0, out[2] = val;
      *outN = 3;
    }
    return (int)i;
  }
  return -1;
}
