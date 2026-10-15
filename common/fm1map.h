/* MIDI mapping rules: what the host box (usbhost/main.c) does to each channel message on its way through.
 * The Raspberry Pi box does the same in usbhost/fm1buddy-midi.py, and the rules are written the same way
 * (see README.md, "Mapping"):
 *
 *   [from NAME] [ch N] [TYPE [NUM]] -> drop
 *   [from NAME] [ch N] [TYPE [NUM]] -> [to NAME] [ch N] [TYPE [NUM]]
 *
 * TYPE is note, cc, pc, at (channel pressure) or pb (pitch bend). The first rule that matches a message
 * decides; a message no rule matches passes as it is. On the right, what is left out stays as it came in.
 * A message's "value" (a note's velocity, a CC's value, the program, the pressure, the bend's top 7 bits)
 * carries over to what it becomes: a CC turned into a note plays it at the CC's value (0 is note off), a CC
 * turned into a program change picks the program from the value (unless the rule names one).
 *
 * On the link (LINK_MAP), a rule is 8 bytes: from and to are device masks (the CYD resolves the names),
 * the rest as in map_rule_t. */
#ifndef FM1MAP_H
#define FM1MAP_H

#include <stdint.h>

#define MAP_RULES 32
#define MAP_ANY 0xFF  /* in_num: any number; out_num: keep it */
#define MAP_DROP 0xFF /* out_type */

enum { MAP_NOTE = 0x9,
       MAP_POLY = 0xA,
       MAP_CC = 0xB,
       MAP_PC = 0xC,
       MAP_AT = 0xD,
       MAP_PB = 0xE };

typedef struct {
  uint8_t from;     /* devices it applies to (bit n = device n), 0xFF: any */
  uint8_t to;       /* devices the result goes to, 0: where the routing sends it */
  uint8_t in_type;  /* 0: any, MAP_NOTE (on and off), MAP_CC, ... */
  uint8_t in_ch;    /* 0: any, 1-16 */
  uint8_t in_num;   /* note or CC number (or program), MAP_ANY: any */
  uint8_t out_type; /* 0: the same, MAP_DROP: drop it */
  uint8_t out_ch;   /* 0: the same */
  uint8_t out_num;  /* MAP_ANY: the same */
} map_rule_t;

/* the message's length from its status byte (channel messages only) */
static inline uint32_t map_len(uint8_t status) {
  uint8_t k = status >> 4;
  return k == 0xC || k == 0xD ? 2 : 3;
}

/* msg: a channel message (status 0x80-0xEF) from device dev. Returns the rule that matched (-1: none, the
 * message passes as it is). When one did, *out_n is 0 to drop it, or out holds what it became. */
static inline int map_apply(const map_rule_t* rules, uint32_t n_rules, uint8_t dev, const uint8_t* msg, uint8_t* out,
                            uint32_t* out_n) {
  uint8_t kind = msg[0] >> 4, ch = (msg[0] & 15) + 1;
  uint8_t d1 = map_len(msg[0]) > 1 ? msg[1] : 0, d2 = map_len(msg[0]) > 2 ? msg[2] : 0;
  int has_num = kind == 0x8 || kind == 0x9 || kind == 0xA || kind == 0xB;
  for (uint32_t i = 0; i < n_rules; i++) {
    const map_rule_t* r = &rules[i];
    if (!(r->from >> dev & 1)) {
      continue;
    }
    if (r->in_ch && r->in_ch != ch) {
      continue;
    }
    if (r->in_type) {
      if (r->in_type == MAP_NOTE ? kind != 0x8 && kind != 0x9 : r->in_type != kind) {
        continue;
      }
      /* the number: a note's, a CC's, or the program */
      if (r->in_num != MAP_ANY && (has_num || kind == 0xC) && d1 != r->in_num) {
        continue;
      }
    }
    if (r->out_type == MAP_DROP) {
      *out_n = 0;
      return (int)i;
    }
    /* the message's number and value */
    uint8_t val = has_num ? d2 : kind == 0xE ? d2
                                             : d1;
    if (kind == 0x8) {
      val = 0;
    }
    uint8_t okind = r->out_type ? r->out_type : kind;
    uint8_t c = (uint8_t)((r->out_ch ? r->out_ch : ch) - 1);
    uint8_t num = r->out_num != MAP_ANY ? r->out_num : has_num ? d1
                                                               : 0;
    if (okind == 0x8 || okind == 0x9) {
      if (r->out_num == MAP_ANY && !has_num) {
        num = 60;
      }
      out[0] = (uint8_t)((val ? 0x90 : 0x80) | c);
      out[1] = num;
      out[2] = val;
      *out_n = 3;
    } else if (okind == MAP_POLY || okind == MAP_CC) {
      out[0] = (uint8_t)(okind << 4 | c);
      out[1] = num;
      out[2] = val;
      *out_n = 3;
    } else if (okind == MAP_PC) {
      out[0] = (uint8_t)(0xC0 | c);
      out[1] = r->out_num != MAP_ANY ? r->out_num : val;
      *out_n = 2;
    } else if (okind == MAP_AT) {
      out[0] = (uint8_t)(0xD0 | c);
      out[1] = val;
      *out_n = 2;
    } else { /* MAP_PB */
      out[0] = (uint8_t)(0xE0 | c);
      out[1] = kind == 0xE ? d1 : 0;
      out[2] = val;
      *out_n = 3;
    }
    return (int)i;
  }
  return -1;
}

#endif
