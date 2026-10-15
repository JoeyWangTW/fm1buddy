/* The fm1buddy USB MIDI host box: an Adafruit Feather RP2040 with USB Type A Host, a USB hub in its USB-A
 * socket, and the FM-1 and controllers on the hub. Whatever any of them sends goes to all of the others, so
 * there is nothing to set up. The red LED blinks with the traffic.
 *
 * The CYD (optional) connects to the Feather's TX/RX: it sees every device and what it sends, sends to any
 * of them, and sets the routing (common/fm1link.h). That is how it installs firmware on the FM-1. */
#include "fm1link.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "pio_usb.h"
#include "tusb.h"

#define PIN_DP 16    /* the USB-A socket's D+ (D- is the next pin) */
#define PIN_5V_EN 18 /* its 5 V */
#define PIN_LED 13
#define LINK_UART uart0
#define PIN_TX 0 /* to the CYD's RX (GPIO22) */
#define PIN_RX 1 /* from the CYD's TX (GPIO27) */

static uint32_t led_off_at;

/* -------------------------------------------------------------- the link: UART RX ring, filled by its IRQ --- */
#define RXQ 4096u
static uint8_t rx_q[RXQ];
static volatile uint32_t rx_w, rx_r;
static link_rx_t rx;

static void uart_irq(void) {
  while (uart_is_readable(LINK_UART)) {
    uint8_t b = (uint8_t)uart_get_hw(LINK_UART)->dr;
    if (rx_w - rx_r < RXQ) {
      rx_q[rx_w % RXQ] = b;
      rx_w++;
    }
  }
}

static void link_send(uint8_t type, uint8_t dev, const uint8_t* p, uint32_t n) {
  uint8_t wire[LINK_WIRE];
  uint32_t len = link_encode(type, dev, p, n, wire);
  uart_write_blocking(LINK_UART, wire, len);
}

/* ---------------------------------------------------------------------------------------- the devices --- */
static struct {
  uint8_t daddr;
  uint8_t named; /* 0: product string not asked yet, 1: asked, 2: have it (MOUNT sent) */
  uint16_t vid, pid;
  uint8_t desc[64]; /* the string descriptor */
  uint8_t out[LINK_PAYLOAD];
  uint32_t out_n, out_at; /* LINK_SEND bytes not written yet */
} dev[CFG_TUH_MIDI];
static uint8_t route[CFG_TUH_MIDI];

static void send_mount(uint8_t idx) {
  uint8_t p[4 + 31];
  uint32_t n = 4;
  p[0] = (uint8_t)dev[idx].vid;
  p[1] = (uint8_t)(dev[idx].vid >> 8);
  p[2] = (uint8_t)dev[idx].pid;
  p[3] = (uint8_t)(dev[idx].pid >> 8);
  if (dev[idx].named == 2) {
    /* UTF-16LE after the 2-byte header: keep printable ASCII */
    uint32_t len = dev[idx].desc[0] <= sizeof dev[idx].desc ? dev[idx].desc[0] : sizeof dev[idx].desc;
    for (uint32_t i = 2; i + 1 < len && n < sizeof p; i += 2) {
      uint8_t c = dev[idx].desc[i];
      if (dev[idx].desc[i + 1] == 0 && c >= 0x20 && c < 0x7F) {
        p[n++] = c;
      }
    }
  }
  link_send(LINK_MOUNT, idx, p, n);
}

static void name_done(tuh_xfer_t* xfer) {
  uint8_t idx = (uint8_t)xfer->user_data;
  if (idx < CFG_TUH_MIDI && tuh_midi_mounted(idx) && dev[idx].daddr == xfer->daddr) {
    if (xfer->result != XFER_RESULT_SUCCESS) {
      dev[idx].desc[0] = 0;
    }
    dev[idx].named = 2;
    send_mount(idx);
  }
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t* info) {
  if (idx >= CFG_TUH_MIDI) {
    return;
  }
  dev[idx].daddr = info->daddr;
  dev[idx].named = 0;
  dev[idx].out_n = dev[idx].out_at = 0;
  tuh_vid_pid_get(info->daddr, &dev[idx].vid, &dev[idx].pid);
}

void tuh_midi_umount_cb(uint8_t idx) {
  if (idx < CFG_TUH_MIDI) {
    dev[idx].named = 0;
    dev[idx].out_n = dev[idx].out_at = 0;
    link_send(LINK_UMOUNT, idx, 0, 0);
  }
}

/* the product string: asked once the control endpoint is free (it is busy during the mount) */
static void ask_names(void) {
  for (uint8_t i = 0; i < CFG_TUH_MIDI; i++) {
    if (tuh_midi_mounted(i) && dev[i].named == 0 &&
        tuh_descriptor_get_product_string(dev[i].daddr, 0x0409, dev[i].desc, sizeof dev[i].desc, name_done, i)) {
      dev[i].named = 1;
    }
  }
}

/* --------------------------------------------------------------------------------------- the traffic --- */
static const uint8_t CIN_LEN[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};

static void forward(void) {
  uint8_t p[4];
  for (uint8_t from = 0; from < CFG_TUH_MIDI; from++) {
    if (!tuh_midi_mounted(from)) {
      continue;
    }
    uint8_t bytes[LINK_PAYLOAD];
    uint32_t nb = 0;
    while (tuh_midi_packet_read(from, p)) {
      p[0] &= 0x0F; /* every message to each device's first port (cable 0) */
      for (uint8_t to = 0; to < CFG_TUH_MIDI; to++) {
        if (to != from && (route[from] >> to & 1) && tuh_midi_mounted(to)) {
          tuh_midi_packet_write(to, p);
        }
      }
      for (uint32_t k = 0; k < CIN_LEN[p[0]]; k++) {
        bytes[nb++] = p[1 + k];
      }
      if (nb > LINK_PAYLOAD - 3) {
        link_send(LINK_MIDI, from, bytes, nb);
        nb = 0;
      }
      gpio_put(PIN_LED, 1);
      led_off_at = to_ms_since_boot(get_absolute_time()) + 20;
    }
    if (nb) {
      link_send(LINK_MIDI, from, bytes, nb);
    }
  }
}

/* what the CYD sent: one LINK_SEND at a time, written as the device's TX buffer has room */
static void from_link(void) {
  for (uint8_t i = 0; i < CFG_TUH_MIDI; i++) {
    if (dev[i].out_at < dev[i].out_n) {
      if (!tuh_midi_mounted(i)) {
        dev[i].out_n = dev[i].out_at = 0;
        continue;
      }
      dev[i].out_at += tuh_midi_stream_write(i, 0, dev[i].out + dev[i].out_at, dev[i].out_n - dev[i].out_at);
      return; /* the rest waits: keeps the order of what the CYD sent */
    }
  }
  while (rx_r != rx_w) {
    uint8_t b = rx_q[rx_r % RXQ];
    rx_r++;
    if (!link_rx_byte(&rx, b)) {
      continue;
    }
    if (rx.type == LINK_HELLO) {
      uint8_t v = LINK_VERSION;
      link_send(LINK_HELLO, 0, &v, 1);
      for (uint8_t i = 0; i < CFG_TUH_MIDI; i++) {
        if (tuh_midi_mounted(i) && dev[i].named == 2) {
          send_mount(i);
        }
      }
    } else if (rx.type == LINK_ROUTE && rx.n >= CFG_TUH_MIDI) {
      for (uint8_t i = 0; i < CFG_TUH_MIDI; i++) {
        route[i] = rx.payload[i];
      }
    } else if (rx.type == LINK_SEND && rx.dev < CFG_TUH_MIDI && tuh_midi_mounted(rx.dev) && rx.n) {
      for (uint32_t k = 0; k < rx.n; k++) {
        dev[rx.dev].out[k] = rx.payload[k];
      }
      dev[rx.dev].out_n = rx.n;
      dev[rx.dev].out_at = tuh_midi_stream_write(rx.dev, 0, dev[rx.dev].out, rx.n);
      return;
    }
  }
}

int main(void) {
  set_sys_clock_khz(120000, true); /* PIO USB needs a multiple of 12 MHz */
  gpio_init(PIN_LED);
  gpio_set_dir(PIN_LED, GPIO_OUT);
  gpio_init(PIN_5V_EN);
  gpio_set_dir(PIN_5V_EN, GPIO_OUT);
  gpio_put(PIN_5V_EN, 1);
  for (uint8_t i = 0; i < CFG_TUH_MIDI; i++) {
    route[i] = LINK_ROUTE_ALL;
  }

  uart_init(LINK_UART, LINK_BAUD);
  gpio_set_function(PIN_TX, GPIO_FUNC_UART);
  gpio_set_function(PIN_RX, GPIO_FUNC_UART);
  uart_set_fifo_enabled(LINK_UART, true);
  irq_set_exclusive_handler(UART0_IRQ, uart_irq);
  irq_set_enabled(UART0_IRQ, true);
  uart_set_irq_enables(LINK_UART, true, false);
  uint8_t v = LINK_VERSION;
  link_send(LINK_HELLO, 0, &v, 1);

  pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
  cfg.pin_dp = PIN_DP;
  tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &cfg);
  tuh_init(BOARD_TUH_RHPORT);

  while (1) {
    tuh_task();
    ask_names();
    forward();
    from_link();
    for (uint8_t to = 0; to < CFG_TUH_MIDI; to++) {
      if (tuh_midi_mounted(to)) {
        tuh_midi_write_flush(to);
      }
    }
    if (led_off_at && (int32_t)(to_ms_since_boot(get_absolute_time()) - led_off_at) >= 0) {
      gpio_put(PIN_LED, 0);
      led_off_at = 0;
    }
  }
}
