/* TinyUSB for the USB MIDI host box: host only, on the PIO USB port, a hub with up to 4 MIDI devices */
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#define CFG_TUSB_MCU OPT_MCU_RP2040
#define CFG_TUSB_OS OPT_OS_PICO
#define CFG_TUSB_DEBUG 0

#define CFG_TUD_ENABLED 0
#define CFG_TUH_ENABLED 1
#define CFG_TUH_RPI_PIO_USB 1
#define BOARD_TUH_RHPORT 1 /* the PIO port: the Feather's USB-A socket */

#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define CFG_TUH_HUB 1
#define CFG_TUH_DEVICE_MAX 5 /* a hub and 4 devices */
#define CFG_TUH_MIDI 4       /* LINK_DEVS */
#define CFG_TUH_MIDI_TX_BUFSIZE 256

#endif
