// TinyUSB (the USB host, usb_s3.cpp), built from source: the framework's own TinyUSB config (its core's USB device
// code is compiled against it too), with the host's settings ours. TinyUSB's DWC2 host takes one of the S3's 8 USB
// channels only while a transfer is going, so more devices fit than with ESP-IDF's host (a channel for every open
// endpoint, all the time)
#pragma once

#include "tusb_config.h"  // the framework's (arduino_tinyusb/include)

#undef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT 0  // the S3 has one USB controller
#define CFG_TUH_MAX_SPEED OPT_MODE_FULL_SPEED

#undef CFG_TUH_ENUMERATION_BUFSIZE
#define CFG_TUH_ENUMERATION_BUFSIZE 512  // config descriptors: a keyboard's can be long
#undef CFG_TUH_DEVICE_MAX
#define CFG_TUH_DEVICE_MAX 8
#undef CFG_TUH_MIDI
#define CFG_TUH_MIDI 4  // midi.h's MIDI_USB
#define CFG_TUH_MIDI_STREAM_API 0
#define CFG_TUH_DWC2_ENDPOINT_MAX 24
#define CFG_TUH_API_EDPT_XFER 1  // the FM-1's boot loader: a storage device spoken to here, without TinyUSB's driver

// not used: their buffers stay out of the internal RAM
#undef CFG_TUH_CDC
#define CFG_TUH_CDC 0
#undef CFG_TUH_HID
#define CFG_TUH_HID 0
#undef CFG_TUH_MSC
#define CFG_TUH_MSC 0
