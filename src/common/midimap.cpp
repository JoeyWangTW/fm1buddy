#include "midimap.h"

#include "link.h"

MapRule midimap::rules[MAP_RULES];
int midimap::count;

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
        w[n][k++] = (char)tolower(*s);
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
    if (!isdigit(*c)) {
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

// [from|to NAME] [ch N] [TYPE [NUM]]: returns the words used, -1 on an error
static int side(char w[][24], int n, bool out, MapRule& r, const char** why) {
  int i = 0;
  if (i < n && !strcmp(w[i], out ? "to" : "from")) {
    if (i + 1 >= n) {
      *why = "a name after from/to";
      return -1;
    }
    strlcpy(out ? r.to : r.from, w[i + 1], sizeof r.from);
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
    if (i < n && isdigit(w[i][0])) {
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
  char buf[128], w[16][24];
  *why = 0;
  strlcpy(buf, line, sizeof buf);
  char* hash = strchr(buf, '#');
  if (hash) {
    *hash = 0;
  }
  memset(&r, 0, sizeof r);
  r.inNum = r.outNum = MAP_ANY;
  char* arrow = strstr(buf, "->");
  if (!arrow) {
    int n = words(buf, w, 16);
    *why = n ? "a rule needs ->" : 0;
    return false;
  }
  *arrow = 0;
  int n = words(buf, w, 16);
  int used = side(w, n, false, r, why);
  if (used < 0) {
    return false;
  }
  if (used < n) {
    *why = "something left over on the left";
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
    *why = "something left over on the right";
    return false;
  }
  return true;
}

static void append(char* out, size_t n, const char* s) {
  if (*out) {
    strlcat(out, " ", n);
  }
  strlcat(out, s, n);
}

static void formatSide(char* out, size_t n, const char* word, const char* name, uint8_t ch, uint8_t type,
                       uint8_t num) {
  char b[32];
  if (*name) {
    append(out, n, word);
    snprintf(b, sizeof b, strchr(name, ' ') ? "\"%s\"" : "%s", name);
    append(out, n, b);
  }
  if (ch) {
    snprintf(b, sizeof b, "ch %u", ch);
    append(out, n, b);
  }
  if (type && TYPE_NAMES[type]) {
    append(out, n, TYPE_NAMES[type]);
    if (num != MAP_ANY) {
      snprintf(b, sizeof b, "%u", num);
      append(out, n, b);
    }
  }
}

void midimap::format(const MapRule& r, char* out, size_t n) {
  *out = 0;
  formatSide(out, n, "from", r.from, r.inCh, r.inType, r.inNum);
  append(out, n, "->");
  if (r.outType == MAP_DROP) {
    append(out, n, "drop");
  } else {
    formatSide(out, n, "to", r.to, r.outCh, r.outType, r.outNum);
  }
}

bool midimap::load(fs::FS& fs) {
  count = 0;
  File f = fs.open(MAP_FILE, "r");
  if (!f) {
    return false;
  }
  char line[128];
  int ln = 0;
  while (f.available() && count < MAP_RULES) {
    size_t k = f.readBytesUntil('\n', line, sizeof line - 1);
    line[k] = 0;
    ln++;
    const char* why;
    if (parse(line, rules[count], &why)) {
      count++;
    } else if (why) {
      Serial.printf(MAP_FILE ":%d: %s\n", ln, why);
    }
  }
  f.close();
  return true;
}

bool midimap::save(fs::FS& fs) {
  File f = fs.open(MAP_FILE, "w");
  if (!f) {
    return false;
  }
  f.print("# fm1buddy MIDI mapping, one rule per line: [from NAME] [ch N] [TYPE [NUM]] -> drop | [to NAME] [ch N] [TYPE [NUM]]\n");
  char line[128];
  for (int i = 0; i < count; i++) {
    format(rules[i], line, sizeof line);
    f.println(line);
  }
  f.close();
  return true;
}

static uint8_t devices(const char* name) {
  uint8_t m = 0;
  for (int d = 0; d < LINK_DEVS; d++) {
    char lower[sizeof hub.dev[d].name];
    int k = 0;
    for (; hub.dev[d].name[k]; k++) {
      lower[k] = (char)tolower(hub.dev[d].name[k]);
    }
    lower[k] = 0;
    if (hub.dev[d].on && strstr(lower, name)) {
      m |= 1 << d;
    }
  }
  return m;
}

void midimap::push() {
  uint8_t p[MAP_RULES * 8];
  for (int i = 0; i < count; i++) {
    const MapRule& r = rules[i];
    uint8_t* b = p + i * 8;
    b[0] = r.from[0] ? devices(r.from) : 0xFF;
    b[1] = r.to[0] ? devices(r.to) : 0;
    b[2] = r.inType;
    b[3] = r.inCh;
    b[4] = r.inNum;
    // "to" a device that isn't there: nowhere, not everywhere
    b[5] = r.to[0] && !b[1] ? MAP_DROP : r.outType;
    b[6] = r.outCh;
    b[7] = r.outNum;
  }
  hub.map(p, count * 8);
}

void midimap::put(int i, const MapRule& r) {
  if (i < 0) {
    if (count < MAP_RULES) {
      rules[count++] = r;
    }
  } else if (i < count) {
    rules[i] = r;
  }
}

void midimap::remove(int i) {
  if (i >= 0 && i < count) {
    memmove(&rules[i], &rules[i + 1], (count - i - 1) * sizeof(MapRule));
    count--;
  }
}

// ------------------------------------------------------------------------------------------- editing ---
static const uint8_t TYPES[] = {0, MAP_NOTE, MAP_CC, MAP_PC, MAP_AT, MAP_PB};

const char* midimap::typeName(uint8_t t) {
  return t < 16 ? TYPE_NAMES[t] : 0;
}

bool midimap::hasNum(uint8_t t) {
  return t == MAP_NOTE || t == MAP_CC || t == MAP_PC;
}

void midimap::blank(MapRule& r) {
  memset(&r, 0, sizeof r);
  r.inNum = r.outNum = MAP_ANY;
}

// the next device name (or "": any) for from/to: the devices on the hub now, and the name the rule has
static void stepName(char* name, size_t n, int dir) {
  char opts[LINK_DEVS + 2][24];
  int k = 0, at = -1;
  opts[k++][0] = 0;
  for (int d = 0; d < LINK_DEVS; d++) {
    if (hub.dev[d].on && hub.dev[d].name[0]) {
      int i = 0;
      for (; hub.dev[d].name[i] && i < 23; i++) {
        opts[k][i] = (char)tolower(hub.dev[d].name[i]);
      }
      opts[k++][i] = 0;
    }
  }
  for (int i = 0; i < k && at < 0; i++) {
    if (!strcmp(opts[i], name)) {
      at = i;
    }
  }
  if (at < 0) {  // a name that isn't on the hub now: keep it in the cycle
    strlcpy(opts[k], name, sizeof opts[k]);
    at = k++;
  }
  strlcpy(name, opts[(at + dir + k) % k], n);
}

static uint8_t stepNum(uint8_t v, int dir) {  // any, 0..127
  if (v == MAP_ANY) {
    return dir > 0 ? 0 : 127;
  }
  int n = v + dir;
  return n < 0 || n > 127 ? MAP_ANY : (uint8_t)n;
}

static uint8_t stepType(uint8_t t, int dir) {
  int i = 0;
  while (i < 6 && TYPES[i] != t) {
    i++;
  }
  return TYPES[(i % 6 + dir + 6) % 6];
}

bool midimap::active(const MapRule& r, int field) {
  uint8_t ot = r.outType ? r.outType : r.inType;  // what it becomes
  if (field >= 4 && r.outType == MAP_DROP) {
    return false;
  }
  if (field == 3) {
    return hasNum(r.inType);
  }
  if (field == 7) {
    return !ot || hasNum(ot);
  }
  return field >= 0 && field < 8;
}

void midimap::step(MapRule& r, int field, int dir) {
  if (!active(r, field)) {
    return;
  }
  switch (field) {
    case 0:
      stepName(r.from, sizeof r.from, dir);
      break;
    case 1:
      r.inCh = (uint8_t)((r.inCh + dir + 17) % 17);
      break;
    case 2:
      r.inType = stepType(r.inType, dir);
      break;
    case 3:
      r.inNum = stepNum(r.inNum, dir);
      break;
    case 4:
      stepName(r.to, sizeof r.to, dir);
      break;
    case 5:
      r.outCh = (uint8_t)((r.outCh + dir + 17) % 17);
      break;
    case 6:
      r.outType = stepType(r.outType, dir);
      break;
    case 7:
      r.outNum = stepNum(r.outNum, dir);
      break;
  }
}

const char* midimap::label(const MapRule& r, int field) {
  static const char* NAMES[] = {"FROM", "CHANNEL", "TYPE", "NUMBER", "TO", "CHANNEL", "TYPE", "NUMBER"};
  uint8_t t = field == 3 ? r.inType : r.outType ? r.outType
                                                : r.inType;
  if ((field == 3 || field == 7) && (t == MAP_NOTE || t == MAP_PC)) {
    return t == MAP_NOTE ? "NOTE" : "PROGRAM";
  }
  return field >= 0 && field < 8 ? NAMES[field] : "";
}

const char* midimap::show(const MapRule& r, int field, char* buf, size_t n) {
  bool drop = r.outType == MAP_DROP;
  if (!active(r, field)) {
    return field >= 4 && drop ? "" : "-";
  }
  uint8_t v;
  switch (field) {
    case 0:
      return r.from[0] ? r.from : "any device";
    case 4:
      return r.to[0] ? r.to : "as routed";
    case 2:
      return r.inType ? typeName(r.inType) : "any";
    case 6:
      return r.outType ? typeName(r.outType) : "same";
    case 1:
    case 5:
      v = field == 1 ? r.inCh : r.outCh;
      if (!v) {
        return field == 1 ? "any" : "same";
      }
      break;
    default:
      v = field == 3 ? r.inNum : r.outNum;
      if (v == MAP_ANY) {
        return field == 3 ? "any" : "same";
      }
  }
  snprintf(buf, n, "%u", v);
  return buf;
}

void midimap::learn(MapRule& r) {
  uint8_t st = hub.last[0], kind = st >> 4;
  int i = 0;
  for (; hub.dev[hub.lastDev].name[i] && i < (int)sizeof r.from - 1; i++) {
    r.from[i] = (char)tolower(hub.dev[hub.lastDev].name[i]);
  }
  r.from[i] = 0;
  r.inCh = (st & 15) + 1;
  r.inType = kind == 0x8 || kind == 0x9 ? MAP_NOTE : kind == 0xA ? 0
                                                                 : kind;
  r.inNum = hasNum(r.inType) ? hub.last[1] : MAP_ANY;
}
