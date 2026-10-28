// SPDX-License-Identifier: MIT
#ifdef FM1BUDDY_RECOVERY_IDF
// S3 RMT port of FM-1-transporter recovery.c / usb_key.pio (MIT).
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#include <soc/gpio_struct.h>
#include <soc/usb_serial_jtag_reg.h>

#include <atomic>

#include "hal.h"
#include "recovery.h"
#define trace hal::log
static std::atomic<int> state{hal::KEY_IDLE};
static void progress(const char *message) {
  hal::log("recovery: %s", message);
}

#include "policy.h"

static constexpr gpio_num_t DP = GPIO_NUM_20, DM = GPIO_NUM_19;
static rmt_channel_handle_t channels[2]{};
static rmt_encoder_handle_t encoders[2]{};
static rmt_sync_manager_handle_t syncer;
static uint32_t signalId[2];
static rmt_symbol_word_t wave[2][17];
static bool enabled[2];
static DRAM_ATTR volatile uint32_t capturedEdges, capturedWord;
static bool checkedPins;
static uint32_t captureLoggedAt;
static bool rmtOk(esp_err_t error, const char *operation, int pin = -1) {
  if (error == ESP_OK)
    return true;
  trace("RMT: %s GPIO%d failed: %s (0x%X)", operation, pin,
        esp_err_to_name(error), unsigned(error));
  return false;
}
static void IRAM_ATTR captureClock(void *) {
  uint32_t count = capturedEdges;
  if (count < 16)
    capturedWord = (capturedWord << 1) | ((GPIO.in >> 19) & 1);
  capturedEdges = count + 1;
}

void releaseRecoveryPins() {
  if (syncer) {
    rmt_del_sync_manager(syncer);
    syncer = nullptr;
  }
  for (int i = 0; i < 2; i++) {
    if (channels[i]) {
      if (enabled[i])
        rmt_disable(channels[i]);
      rmt_del_channel(channels[i]);
      channels[i] = nullptr;
      enabled[i] = false;
    }
    if (encoders[i]) {
      rmt_del_encoder(encoders[i]);
      encoders[i] = nullptr;
    }
  }
  for (auto pin : {DP, DM}) {
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_FLOATING);
  }
}
static void releaseLines() {
  gpio_set_direction(DP, GPIO_MODE_INPUT);
  gpio_set_direction(DM, GPIO_MODE_INPUT);
}
static bool setupKey() {
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG,
                      USB_SERIAL_JTAG_USB_PAD_ENABLE | USB_SERIAL_JTAG_DP_PULLUP);
  KeyBit bits[16];
  key_bits(bits);
  for (int i = 0; i < 2; i++) {
    auto pin = i ? DM : DP;
    rmt_tx_channel_config_t c{};
    c.gpio_num = pin;
    c.clk_src = RMT_CLK_SRC_XTAL;
    c.resolution_hz = 1000000;
    c.mem_block_symbols = 64;
    c.trans_queue_depth = 1;
    c.flags.io_loop_back = true;
    if (!rmtOk(rmt_new_tx_channel(&c, &channels[i]), "new key channel", pin))
      return false;
    signalId[i] = GPIO.func_out_sel_cfg[pin].func_sel;
    rmt_copy_encoder_config_t e{};
    if (!rmtOk(rmt_new_copy_encoder(&e, &encoders[i]), "new key encoder", pin))
      return false;
    if (!rmtOk(rmt_enable(channels[i]), "enable key channel", pin))
      return false;
    enabled[i] = true;
    gpio_set_drive_capability(pin, GPIO_DRIVE_CAP_0);
    gpio_set_pull_mode(pin, GPIO_PULLDOWN_ONLY);
    wave[i][0] = {};
    wave[i][0].duration0 = 2;
    wave[i][0].duration1 = 2;
    for (int b = 0; b < 16; b++) {
      auto &s = wave[i][b + 1];
      s = {};
      s.duration0 = bits[b].low_us;
      s.duration1 = bits[b].high_us;
      s.level0 = i ? bits[b].data : 0;
      s.level1 = i ? bits[b].data : 1;
    }
  }
  rmt_sync_manager_config_t c{};
  c.tx_channel_array = channels;
  c.array_size = 2;
  return rmtOk(rmt_new_sync_manager(&c, &syncer), "new key sync manager");
}
static bool packet() {
  for (int i = 0; i < 2; i++) {
    auto pin = i ? DM : DP;
    // gpio_set_direction(OUTPUT) resets the matrix to plain GPIO in IDF.
    // Restore the RMT route AFTER enabling output, never before it.
    gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT);
    esp_rom_gpio_connect_out_signal(pin, signalId[i], false, false);
    if (signalId[i] == SIG_GPIO_OUT_IDX || GPIO.func_out_sel_cfg[pin].func_sel != signalId[i]) {
      trace("KEY: RMT route lost on GPIO%d", int(pin));
      return false;
    }
  }
  if (rmt_sync_reset(syncer) != ESP_OK)
    return false;
  bool checking = !checkedPins;
  if (checking) {
    capturedEdges = capturedWord = 0;
    if (gpio_install_isr_service(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3) != ESP_OK) {
      trace("KEY: cannot install pin self-check");
      return false;
    }
    if (gpio_set_intr_type(DP, GPIO_INTR_POSEDGE) != ESP_OK ||
        gpio_isr_handler_add(DP, captureClock, nullptr) != ESP_OK ||
        gpio_intr_enable(DP) != ESP_OK) {
      gpio_uninstall_isr_service();
      return false;
    }
  }
  rmt_transmit_config_t tx{};
  bool sent = true;
  for (int i = 0; i < 2; i++)
    if (rmt_transmit(channels[i], encoders[i], wave[i], sizeof wave[i], &tx) != ESP_OK)
      sent = false;
  for (int i = 0; i < 2; i++)
    if (rmt_tx_wait_all_done(channels[i], 20) != ESP_OK)
      sent = false;
  if (checking)
    gpio_intr_disable(DP);
  releaseLines();  // Release immediately, before logging/tearing down the observer.
  if (checking) {
    gpio_isr_handler_remove(DP);
    gpio_uninstall_isr_service();
    uint32_t edges = capturedEdges, word = capturedWord;
    checkedPins = edges == 16 && word == 0x16EF;
    if (checkedPins || !captureLoggedAt || millis() - captureLoggedAt >= 2000) {
      trace("KEY: pin self-check edges=%lu/16 key=%04lX/16EF routes=%lu,%lu %s",
            (unsigned long)edges, (unsigned long)word, (unsigned long)signalId[0],
            (unsigned long)signalId[1], checkedPins ? "PASS" : "waiting");
      captureLoggedAt = millis();
    }
    // The powered-off target can clamp its data pins. Keep trying within the
    // existing 30 s window; require a valid capture before accepting any ACK.
  }
  if (!sent)
    return false;
  esp_rom_delay_us(18);
  return true;
}
static bool probeDm() {
  esp_rom_gpio_connect_out_signal(DM, SIG_GPIO_OUT_IDX, false, false);
  gpio_set_level(DM, 1);
  gpio_set_direction(DM, GPIO_MODE_INPUT_OUTPUT);
  esp_rom_delay_us(2);
  bool high = gpio_get_level(DM);
  gpio_set_direction(DM, GPIO_MODE_INPUT);
  gpio_set_level(DM, 0);
  return high;
}
static bool calibration() {
  // Replace paired key channels with an infinite, open-drain 4 us / 996 us waveform.
  releaseRecoveryPins();
  rmt_tx_channel_config_t c{};
  c.gpio_num = DP;
  c.clk_src = RMT_CLK_SRC_XTAL;
  c.resolution_hz = 1000000;
  c.mem_block_symbols = 64;
  c.trans_queue_depth = 1;
  c.flags.io_od_mode = true;
  c.flags.io_loop_back = true;
  rmt_copy_encoder_config_t e{};
  if (!rmtOk(rmt_new_tx_channel(&c, &channels[0]), "new calibration channel", DP) ||
      !rmtOk(rmt_new_copy_encoder(&e, &encoders[0]), "new calibration encoder", DP) ||
      !rmtOk(rmt_enable(channels[0]), "enable calibration channel", DP))
    return false;
  enabled[0] = true;
  static rmt_symbol_word_t pulse{};
  pulse.duration0 = 4;
  pulse.level0 = 0;
  pulse.duration1 = 996;
  pulse.level1 = 1;
  rmt_transmit_config_t tx{};
  tx.loop_count = -1;
  tx.flags.eot_level = 1;
  if (rmt_transmit(channels[0], encoders[0], &pulse, sizeof pulse, &tx) != ESP_OK)
    return false;
  capturedEdges = capturedWord = 0;
  bool observing = gpio_install_isr_service(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3) == ESP_OK;
  if (observing) {
    gpio_set_intr_type(DP, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(DP, captureClock, nullptr);
    gpio_intr_enable(DP);
  }
  trace("PULSE: RMT open drain, 4us low / 996us released; observer=%d", observing);
  state = hal::KEY_ANSWERED;
  progress("ROM clock calibration");
  unsigned lows = 0;
  uint32_t start = millis();
  while (millis() - start < 6000) {
    if (!gpio_get_level(DP)) {
      if (++lows >= 3 && millis() - start >= 1000)
        break;
    } else
      lows = 0;
    esp_rom_delay_us(100);
    // Let the scheduler run occasionally; pulse timing remains in hardware.
    if ((millis() - start) % 50 == 0)
      vTaskDelay(1);
  }
  if (observing) {
    gpio_intr_disable(DP);
    gpio_isr_handler_remove(DP);
    gpio_uninstall_isr_service();
  }
  trace("PULSE: handoff after %lu ms, D+=%d rising_edges=%lu", (unsigned long)(millis() - start),
        gpio_get_level(DP), (unsigned long)capturedEdges);
  return true;
}
bool recoveryKey() {
  if (!setupKey()) {
    trace("KEY: RMT initialization failed");
    releaseRecoveryPins();
    return false;
  }
  state = hal::KEY_SENDING;
  progress("Switch FM-1 ON now");
  trace("KEY: 0x16EF MSB first; GPIO20 D+, GPIO19 D-; 30 second limit");
  bool alive = false;
  uint32_t start = millis(), said = start, count = 0;
  while (millis() - start < 30000) {
    if (!packet()) {
      trace("KEY: RMT packet failed");
      break;
    }
    ++count;
    int low = 0, high = 0;
    bool acknowledged = false;
    for (int j = 0; j < 4; j++) {
      esp_rom_delay_us(35);
      high = gpio_get_level(DP) ? high + 1 : 0;
      if (high >= 2) {
        trace("KEY: D+ high in gap");
        acknowledged = true;
        break;
      }
      if (probeDm()) {
        alive = true;
        low = 0;
      } else if (alive && ++low >= 2) {
        trace("KEY: possible ACK (D- held low)");
        unsigned consecutive = 0;
        uint32_t t = millis();
        while (millis() - t < 500) {
          consecutive = gpio_get_level(DP) ? consecutive + 1 : 0;
          if (consecutive >= 5) {
            acknowledged = true;
            break;
          }
          esp_rom_delay_us(50);
        }
        break;
      }
    }
    if (acknowledged && checkedPins) {
      trace("KEY: pull-up after %lu packets", (unsigned long)count);
      if (calibration())
        return true;
      break;
    }
    if (millis() - said >= 2000) {
      trace("KEY: %lu packets D+=%d D-=%d probeRose=%d", (unsigned long)count, gpio_get_level(DP),
            gpio_get_level(DM), alive);
      said = millis();
    }
    vTaskDelay(1);
  }
  trace("KEY: %s; pin self-check=%s",
        "no qualified response / timeout",
        checkedPins ? "PASS" : "NOT PASSED");
  releaseRecoveryPins();
  return false;
}

static void keyTask(void *) {
  // One host stack per boot. This task owns the IDF client through enumeration;
  // KEY_HOST hands it to the single backup job after this task stops using it.
  bool ok = !hal::onComputer() && recovery::prepare() && recoveryKey() &&
            recovery::startHost() && recovery::waitHost(15000);
  if (!ok) {
    releaseRecoveryPins();
    recovery::stop();
    hal::log("recovery: key or enumeration failed; no flash writes");
  }
  state = ok ? hal::KEY_HOST : hal::KEY_FAILED;
  vTaskDelete(nullptr);
}
void hal::keyStart() {
  int idle = KEY_IDLE;
  if (recoveryMode() && state.compare_exchange_strong(idle, KEY_OFF)) {
    if (onComputer() || xTaskCreatePinnedToCore(keyTask, "recovery-key", 8192, nullptr, 3, nullptr, 0) != pdPASS)
      state = KEY_FAILED;
  }
}
int hal::keyState() { return state.load(); }

#endif
