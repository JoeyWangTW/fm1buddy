/* fm1buddy link: the CYD (cyd/) and the USB host box (usbhost/) talk over a UART, 8N1 at LINK_BAUD.
 *
 * Each frame is COBS-encoded and ends with a 0x00. Decoded, it is:
 *   type, dev, payload (0..LINK_PAYLOAD bytes), crc8 (poly 0x07, over type, dev and payload)
 *
 * box -> CYD
 *   LINK_HELLO   dev 0, payload: LINK_VERSION. Sent at boot, and as the answer to the CYD's HELLO.
 *   LINK_MOUNT   a USB MIDI device came (or, after a HELLO, is there): vid lo hi, pid lo hi, product name
 *   LINK_UMOUNT  it went
 *   LINK_MIDI    MIDI bytes the device sent (every cable, as one stream)
 * CYD -> box
 *   LINK_HELLO   dev 0: answer with HELLO, then a MOUNT for every device that is there
 *   LINK_SEND    MIDI bytes for the device (cable 0)
 *   LINK_ROUTE   dev 0, payload: LINK_DEVS bytes, which devices each device's messages go to (bit n = dev n)
 *   LINK_MAP     dev 0, payload: the mapping rules (common/fm1map.h), 8 bytes each, all of them (none: clear)
 *
 * Devices 0..LINK_USB-1 are on the box's hub: the box's TinyUSB MIDI index. Devices LINK_USB..LINK_DEVS-1 are
 * the CYD's own (Bluetooth MIDI), and the frames go the other way:
 * CYD -> box
 *   LINK_MOUNT   one of them came: vid, pid (0), its name
 *   LINK_UMOUNT  it went
 *   LINK_MIDI    MIDI bytes it sent: the box routes and maps them as if from a device on the hub
 * box -> CYD
 *   LINK_SEND    MIDI bytes for it
 * The box tells everyone about them as about its own (MOUNT, UMOUNT and MIDI back), and forgets them when the
 * CYD says HELLO; the CYD mounts them again after the box's HELLO.
 *
 * A device stays the same while it is plugged in (or connected), and is not the same device after a
 * LINK_UMOUNT. */
#ifndef FM1LINK_H
#define FM1LINK_H

#include <stdint.h>

#define LINK_BAUD 1000000
#define LINK_VERSION 2 /* 2: the CYD's own devices */
#define LINK_USB 4     /* devices on the box's hub */
#define LINK_DEVS 8    /* and the CYD's: all of them fit a byte, as in LINK_ROUTE and the mapping */
#define LINK_PAYLOAD 256
#define LINK_FRAME (2 + LINK_PAYLOAD + 1)             /* decoded */
#define LINK_WIRE (LINK_FRAME + LINK_FRAME / 254 + 2) /* encoded, with the 0x00 */
#define LINK_ROUTE_ALL 0xFF

enum {
  LINK_HELLO = 1,
  LINK_MOUNT = 2,
  LINK_UMOUNT = 3,
  LINK_MIDI = 4,
  LINK_SEND = 5,
  LINK_ROUTE = 6,
  LINK_MAP = 7
};

static inline uint8_t link_crc8(const uint8_t* p, uint32_t n) {
  uint8_t c = 0;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; i++) {
      c = c & 0x80 ? (uint8_t)(c << 1 ^ 0x07) : (uint8_t)(c << 1);
    }
  }
  return c;
}

/* one frame onto out (LINK_WIRE bytes), with its 0x00; returns its length */
static inline uint32_t link_encode(uint8_t type, uint8_t dev, const uint8_t* payload, uint32_t n, uint8_t* out) {
  uint8_t raw[LINK_FRAME];
  uint32_t len = 0, o = 1, code_at = 0;
  uint8_t code = 1;
  if (n > LINK_PAYLOAD) {
    n = LINK_PAYLOAD;
  }
  raw[len++] = type;
  raw[len++] = dev;
  for (uint32_t i = 0; i < n; i++) {
    raw[len++] = payload[i];
  }
  raw[len] = link_crc8(raw, len);
  len++;
  for (uint32_t i = 0; i < len; i++) {
    if (raw[i] == 0) {
      out[code_at] = code;
      code_at = o++;
      code = 1;
    } else {
      out[o++] = raw[i];
      if (++code == 0xFF) {
        out[code_at] = code;
        code_at = o++;
        code = 1;
      }
    }
  }
  out[code_at] = code;
  out[o++] = 0;
  return o;
}

/* a receiver: feed it every byte; link_rx_byte returns 1 when a good frame is in f (type, dev, n, payload) */
typedef struct {
  uint8_t wire[LINK_WIRE];
  uint32_t wn;
  uint8_t type, dev;
  uint32_t n;
  uint8_t payload[LINK_FRAME];
  uint32_t bad; /* frames dropped: too long, bad COBS or CRC */
} link_rx_t;

static inline int link_rx_byte(link_rx_t* f, uint8_t b) {
  if (b != 0) {
    if (f->wn < LINK_WIRE) {
      f->wire[f->wn] = b;
    }
    f->wn++;
    return 0;
  }
  uint32_t wn = f->wn, i = 0, o = 0;
  f->wn = 0;
  if (wn == 0) {
    return 0;
  }
  if (wn > LINK_WIRE) {
    f->bad++;
    return 0;
  }
  while (i < wn) {
    uint8_t code = f->wire[i++];
    for (uint8_t j = 1; j < code; j++) {
      if (i >= wn || o >= LINK_FRAME) {
        f->bad++;
        return 0;
      }
      f->payload[o++] = f->wire[i++];
    }
    if (code < 0xFF && i < wn) {
      if (o >= LINK_FRAME) {
        f->bad++;
        return 0;
      }
      f->payload[o++] = 0;
    }
  }
  if (o < 3 || link_crc8(f->payload, o - 1) != f->payload[o - 1]) {
    f->bad++;
    return 0;
  }
  f->type = f->payload[0];
  f->dev = f->payload[1];
  f->n = o - 3;
  for (uint32_t k = 0; k < f->n; k++) {
    f->payload[k] = f->payload[k + 2];
  }
  return 1;
}

#endif
