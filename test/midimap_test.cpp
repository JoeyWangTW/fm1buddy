// midimap's rules: parsing, writing back, and what they do to messages. tools/unit-test.sh builds and runs it
#include "midimap.h"

#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(c)                                     \
  do {                                               \
    if (!(c)) {                                      \
      printf("%s:%d: %s\n", __FILE__, __LINE__, #c); \
      fails++;                                       \
    }                                                \
  } while (0)

static MapRule rule(const char* line) {
  MapRule r;
  const char* why;
  if (!midimap::parse(line, r, &why)) {
    printf("doesn't parse: %s (%s)\n", line, why ? why : "-");
    fails++;
  }
  return r;
}

static bool bad(const char* line) {
  MapRule r;
  const char* why;
  return !midimap::parse(line, r, &why) && why;
}

// msg through one rule from device 0, every device matching its names; out and n back
static int run(const MapRule& r, uint8_t a, uint8_t b, uint8_t c, uint8_t* out, uint32_t* n) {
  std::vector<MapRule> rules{r};
  uint32_t from = 1, to = r.to[0] ? 2 : 0, mask;
  uint8_t msg[3] = {a, b, c};
  return midimap::apply(rules, &from, &to, 0, msg, out, n, &mask);
}

int main() {
  uint8_t o[3];
  uint32_t n;

  // a range splits a keyboard
  MapRule lo = rule("from keystep note 0-59 -> to fm-1 ch 1 +12");
  CHECK(lo.inNum == 0 && lo.inHi == 59 && lo.transpose == 12 && lo.outCh == 1);
  CHECK(run(lo, 0x90, 48, 100, o, &n) == 0 && n == 3 && o[0] == 0x90 && o[1] == 60 && o[2] == 100);
  CHECK(run(lo, 0x80, 48, 0, o, &n) == 0 && n == 3 && o[0] == 0x80 && o[1] == 60);
  CHECK(run(lo, 0x90, 60, 100, o, &n) == -1);  // above the range: not this rule

  // moved off the keyboard: nothing
  MapRule down = rule("note -> -24");
  CHECK(run(down, 0x90, 10, 100, o, &n) == 0 && n == 0);
  CHECK(run(down, 0x90, 50, 100, o, &n) == 0 && n == 3 && o[1] == 26);

  // velocity: fixed, and scaled (note offs stay as they are)
  MapRule fixed = rule("note -> vel 100");
  CHECK(fixed.vel == 100);
  CHECK(run(fixed, 0x91, 60, 20, o, &n) == 0 && o[0] == 0x91 && o[2] == 100);
  CHECK(run(fixed, 0x91, 60, 0, o, &n) == 0 && o[0] == 0x81);  // a note on at 0 is an off
  MapRule half = rule("note -> vel 50%");
  CHECK(half.velPct == 50);
  CHECK(run(half, 0x90, 60, 100, o, &n) == 0 && o[2] == 50);
  CHECK(run(half, 0x90, 60, 1, o, &n) == 0 && o[2] == 1);  // never 0: that would be an off
  MapRule loud = rule("note -> vel 200%");
  CHECK(run(loud, 0x90, 60, 100, o, &n) == 0 && o[2] == 127);

  // CC ranges
  MapRule ccs = rule("cc 20-29 -> ch 3");
  CHECK(run(ccs, 0xB0, 25, 64, o, &n) == 0 && o[0] == 0xB2 && o[1] == 25);
  CHECK(run(ccs, 0xB0, 30, 64, o, &n) == -1);

  // what's wrong
  CHECK(bad("note 60-40 -> drop"));
  CHECK(bad("note -> vel 0"));
  CHECK(bad("note -> vel 500%"));
  CHECK(bad("note -> +200"));
  CHECK(bad("note -> to"));

  // written back as it reads
  const char* lines[] = {"from keystep note 0-59 -> to fm-1 ch 1 +12", "from \"launchkey mini\" -> to fm-1 ch 2",
                         "cc 1 -> pc", "pb -> drop", "ch 10 note 36 -> note 38 vel 90%", "note -> -7 vel 64"};
  for (const char* l : lines) {
    std::string t = midimap::text(rule(l));
    if (t != l) {
      printf("written back as \"%s\", not \"%s\"\n", t.c_str(), l);
      fails++;
    }
  }
  CHECK(midimap::describe(lo) == "keystep, notes 0-59 -> fm-1 ch 1, +12");
  CHECK(midimap::describe(rule("pb -> drop")) == "anything, pitch bend -> dropped");

  // a line too long for a rule (a damaged file): skipped whole, its line number right, the rules around it kept
  {
    const char* path = "build/midimap_long.txt";
    FILE* f = fopen(path, "w");
    fprintf(f, "cc 1 -> pc\n");
    for (int i = 0; i < 400; i++) {
      fputc(i < 300 ? ' ' : 'x', f);
    }
    fprintf(f, " -> drop\npb -> drop\n");
    fclose(f);
    std::vector<MapRule> rules;
    std::string problem;
    CHECK(midimap::load(path, rules, problem));
    CHECK(rules.size() == 2);
    CHECK(problem == "line 2: too long");
    CHECK(!midimap::damaged(path));
    // sectors that never made it to the card: zeros
    f = fopen(path, "r+b");
    fputc(0, f);
    fclose(f);
    CHECK(midimap::damaged(path));
    remove(path);
  }
  CHECK(!midimap::damaged("build/no-such-file.txt"));

  printf(fails ? "midimap: %d failed\n" : "midimap: ok\n", fails);
  return fails != 0;
}
