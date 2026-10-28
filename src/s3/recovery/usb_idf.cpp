// SPDX-License-Identifier: MIT
// Adapted from the tested Hosyond S3 Transporter backend; see LICENSE.
#ifdef FM1BUDDY_RECOVERY_IDF
#include <Arduino.h>
#include <driver/gpio.h>
#include <hal/usb_wrap_ll.h>
#include <soc/usb_dwc_struct.h>
#include <usb/usb_host.h>

#include <atomic>
#include <cstring>

#include "hal.h"
#include "recovery.h"
#define trace hal::log
#include "policy.h"

extern void releaseRecoveryPins();
static usb_host_client_handle_t client;
static usb_device_handle_t device;
static usb_transfer_t *transfer;
static uint8_t pendingAddress, epIn, epOut, interfaceNumber;
static bool gone, broken, ready, completed;
static uint32_t tag;
static unsigned epInMps;
static std::atomic<bool> rejectedDevice{false};
static uint8_t buffer[4096];
static void events(const usb_host_client_event_msg_t *m, void *) {
  if (m->event == USB_HOST_CLIENT_EVENT_NEW_DEV && !device)
    pendingAddress = m->new_dev.address;
  if (m->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    gone = true;
    ready = false;
  }
}
static void transferDone(usb_transfer_t *) {
  completed = true;
}
static void daemon(void *) {
  for (;;) {
    uint32_t flags;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
  }
}
static void pump() {
  usb_host_client_handle_events(client, pdMS_TO_TICKS(1));
}
static bool io(uint8_t ep, void *bytes, unsigned len, unsigned *actual = nullptr) {
  if (!device || gone || broken || len > 4096)
    return false;
  transfer->device_handle = device;
  transfer->bEndpointAddress = ep;
  transfer->num_bytes = (ep & 0x80) ? usb_in_size(len, epInMps) : len;
  if (!transfer->num_bytes || transfer->num_bytes > 4096)
    return false;
  transfer->callback = transferDone;
  if (!(ep & 0x80))
    memcpy(transfer->data_buffer, bytes, len);
  completed = false;
  esp_err_t e = usb_host_transfer_submit(transfer);
  if (e != ESP_OK) {
    trace("USB: submit ep=%02X error=%s", ep, esp_err_to_name(e));
    broken = true;
    return false;
  }
  uint32_t start = millis();
  while (!completed && millis() - start < 2500 && !gone)
    pump();
  if (!completed) {
    trace("USB: timeout/disconnect ep=%02X; aborting session", ep);
    broken = true;
    usb_host_endpoint_halt(device, ep);
    usb_host_endpoint_flush(device, ep);
    // Transfer storage remains alive until reset, even if cancellation never completes.
    start = millis();
    while (!completed && millis() - start < 1000)
      pump();
    return false;
  }
  if (transfer->status != USB_TRANSFER_STATUS_COMPLETED || transfer->actual_num_bytes > int(len)) {
    trace("USB: ep=%02X status=%d bytes=%d/%u", ep, transfer->status, transfer->actual_num_bytes,
          len);
    broken = true;
    return false;
  }
  if (actual)
    *actual = transfer->actual_num_bytes;
  if (ep & 0x80)
    memcpy(bytes, transfer->data_buffer, transfer->actual_num_bytes);
  return true;
}
static void le32(uint8_t *p, uint32_t n) {
  for (int i = 0; i < 4; i++)
    p[i] = n >> (8 * i);
}
static bool bot(const uint8_t *c, uint8_t n, bool in, void *data, uint16_t len, uint32_t *got) {
  if (got)
    *got = 0;
  if (!command_allowed(c, n, in, len)) {
    trace("POLICY: refused CDB %02X%02X", c ? c[0] : 0, c && n > 1 ? c[1] : 0);
    return false;
  }
  uint8_t cbw[31]{};
  memcpy(cbw, "USBC", 4);
  uint32_t current = ++tag;
  le32(cbw + 4, current);
  le32(cbw + 8, len);
  cbw[12] = in ? 0x80 : 0;
  cbw[14] = n;
  memcpy(cbw + 15, c, n);
  unsigned actual = 0;
  if (!io(epOut, cbw, 31, &actual) || actual != 31) {
    broken = true;
    return false;
  }
  if (len) {
    if (!io(in ? epIn : epOut, data, len, &actual) || actual != len) {
      broken = true;
      return false;
    }
    if (got)
      *got = actual;
  }
  uint8_t csw[13]{};
  if (!io(epIn, csw, sizeof csw, &actual) || actual != 13 || memcmp(csw, "USBS", 4) ||
      !valid_csw(csw, actual, current)) {
    trace("USB: invalid/failed CSW; session stopped");
    broken = true;
    return false;
  }
  return true;
}
bool recovery::command(const uint8_t *c, uint8_t n, bool in, uint8_t *data, uint32_t len, uint32_t *got) {
  if (got)
    *got = 0;
  return len <= 4096 && ready && !broken && !gone && bot(c, n, in, data, len, got);
}
bool recovery::present(uint16_t vid, uint16_t pid) {
  return vid == 0x4C4A && pid == 0x8057 && ready && !gone && !broken;
}
bool recovery::seen(uint16_t vid, uint16_t pid) {
  return vid == 0x4C4A && pid == 0x8057 && device && !gone;
}
void recovery::stop() {
  broken = true;
  trace("recovery: session stopped; reset required, no retry");
}
static bool openDevice() {
  uint8_t address = pendingAddress;
  pendingAddress = 0;
  if (usb_host_device_open(client, address, &device) != ESP_OK)
    return false;
  const usb_device_desc_t *dd;
  if (usb_host_get_device_descriptor(device, &dd) != ESP_OK)
    return false;
  trace("USB: VID=%04X PID=%04X", dd->idVendor, dd->idProduct);
  if (dd->idVendor != 0x4C4A || dd->idProduct != 0x8057) {
    trace("USB: not WL82 boot ROM; refusing");
    return false;
  }
  const usb_config_desc_t *cfg;
  if (usb_host_get_active_config_descriptor(device, &cfg) != ESP_OK)
    return false;
  const uint8_t *raw = (const uint8_t *)cfg;
  bool selected = false;
  unsigned matches = 0;
  for (unsigned off = 0; off + 2 <= cfg->wTotalLength;) {
    uint8_t len = raw[off], type = raw[off + 1];
    if (len < 2 || off + len > cfg->wTotalLength)
      return false;
    if (type == 4 && len >= 9) {
      selected =
          raw[off + 5] == 8 && raw[off + 6] == 6 && raw[off + 7] == 0x50 && raw[off + 3] == 0;
      if (selected) {
        if (++matches > 1)
          return false;
        interfaceNumber = raw[off + 2];
        epIn = epOut = 0;
      }
    }
    if (type == 5 && len >= 7 && selected && (raw[off + 3] & 3) == 2) {
      if (raw[off + 2] & 0x80) {
        if (epIn)
          return false;
        epIn = raw[off + 2];
        epInMps = raw[off + 4] | raw[off + 5] << 8;
      } else {
        if (epOut)
          return false;
        epOut = raw[off + 2];
      }
    }
    off += len;
  }
  if (matches != 1 || !epIn || !epOut ||
      (epInMps != 8 && epInMps != 16 && epInMps != 32 && epInMps != 64))
    return false;
  if (usb_host_interface_claim(client, device, interfaceNumber, 0) != ESP_OK)
    return false;
  trace("USB: MSC interface=%u in=%02X out=%02X", interfaceNumber, epIn, epOut);
  // Transporter waits before the ROM's first bulk command.
  uint32_t t = millis();
  while (millis() - t < 1500 && !gone)
    pump();
  uint8_t inquiry[6] = {0x12, 0, 0, 0, 36, 0};
  memset(buffer, 0, 36);
  uint32_t got;
  if (!bot(inquiry, 6, true, buffer, 36, &got))
    return false;
  trace("USB: INQUIRY vendor=%.8s product=%.16s rev=%.4s", buffer + 8, buffer + 16, buffer + 32);
  if (!rom_inquiry(buffer, got)) {
    trace("USB: INQUIRY is not WL82 / UBOOT1.00 / 1.00");
    return false;
  }
  trace("USB: WL82 UBOOT1.00 INQUIRY verified");
  ready = true;
  return true;
}
static void portState(const char *label) {
  usb_dwc_hprt_reg_t p;
  p.val = USB_DWC.hprt_reg.val;
  trace(
      "USB %s: port=%08lX conn=%u enabled=%u lines=%u power=%u frame=%u pad=%u mux=%u D+=%d D-=%d",
      label, (unsigned long)p.val, p.prtconnsts, p.prtena, p.prtlnsts, p.prtpwr,
      USB_DWC.hfnum_reg.frnum, USB_WRAP.otg_conf.pad_enable, RTCCNTL.usb_conf.sw_usb_phy_sel,
      gpio_get_level(GPIO_NUM_20), gpio_get_level(GPIO_NUM_19));
}
static bool enumIdentity(const usb_device_desc_t *d, uint8_t *configuration) {
  trace("ENUM: device descriptor VID=%04X PID=%04X ep0=%u", d->idVendor, d->idProduct,
        d->bMaxPacketSize0);
  *configuration = 1;
  bool expected = d->idVendor == 0x4C4A && d->idProduct == 0x8057;
  if (!expected) {
    // The daemon invokes this callback; publish the result to the key task.
    // A running instrument can also pull D+ high. That alone is not ROM proof.
    rejectedDevice = true;
    trace("ENUM: unexpected device; recovery loader required, no commands sent");
  }
  return expected;
}
bool recovery::prepare() {
  if (usb_host_transfer_alloc(4096, 0, &transfer) != ESP_OK)
    return false;
  usb_host_config_t h{};
  h.intr_flags = ESP_INTR_FLAG_LEVEL1;
  h.root_port_unpowered = true;
  h.enum_filter_cb = enumIdentity;
  esp_err_t e = usb_host_install(&h);
  if (e != ESP_OK) {
    trace("USB host prepare: %s", esp_err_to_name(e));
    return false;
  }
  // The library/PHY is ready, but its root port is inactive. Lend the pads to
  // RMT until the key + calibration complete; no USB frames run during the key.
  usb_wrap_ll_phy_enable_pad(&USB_WRAP, false);
  usb_wrap_ll_phy_disable_pull_override(&USB_WRAP);
  usb_host_client_config_t c{};
  c.max_num_event_msg = 8;
  c.async.client_event_callback = events;
  if (usb_host_client_register(&c, &client) != ESP_OK)
    return false;
  if (xTaskCreatePinnedToCore(daemon, "usb-events", 4096, nullptr, 3, nullptr, 0) != pdPASS)
    return false;
  portState("prepared (pads released)");
  return true;
}
bool recovery::startHost() {
  // Do all potentially slow root-port work while the RMT pulses still run.
  esp_err_t e = usb_host_lib_set_root_port_power(true);
  if (e != ESP_OK) {
    trace("USB root port start: %s", esp_err_to_name(e));
    releaseRecoveryPins();
    return false;
  }
  uint32_t t = micros();
  releaseRecoveryPins();
  usb_wrap_pull_override_vals_t pulls{};
  pulls.dp_pd = true;
  pulls.dm_pd = true;
  usb_wrap_ll_phy_enable_pull_override(&USB_WRAP, &pulls);
  usb_wrap_ll_phy_enable_pad(&USB_WRAP, true);
  trace("USB: preinitialized host handoff in %lu us", (unsigned long)(micros() - t));
  portState("handoff");
  return true;
}
bool recovery::waitHost(unsigned ms) {
  uint32_t start = millis(), said = start;
  while (millis() - start < ms) {
    pump();
    if (rejectedDevice.load()) {
      trace("USB: device detected but not the boot ROM; stopping without commands");
      return false;
    }
    if (pendingAddress)
      return openDevice();
    if (millis() - said >= 1000) {
      portState("waiting");
      said = millis();
    }
  }
  trace("USB: no boot-ROM device within %u ms", ms);
  return false;
}

#endif
