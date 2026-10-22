// hal.h on the CYD-S3: the SD card (4-bit SD_MMC, at /sdcard for stdio), NVS for the settings, Wi-Fi, and HTTPS
// with ESP-IDF's certificate bundle
#include <Arduino.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <mbedtls/platform.h>
#include <mbedtls/sha256.h>
#include <soc/rtc_cntl_reg.h>
#include <sys/stat.h>

#include <algorithm>

#include "board.h"
#include "hal.h"

static bool sdOk, logToSd, computer;
// the console's "host": the next start is the USB host even with a computer on it, for a while (testing)
#define HOST_TEST 0x484F5354
static RTC_NOINIT_ATTR uint32_t hostTest;
static uint32_t hostTestUntil;
#define LOG_FILE "/sdcard/fm1buddy/log.txt"
static volatile bool wifiUp, radio;

void board::begin() {
  // (not heap_caps_malloc_extmem_enable(): small blocks must stay internal, the USB host's DMA can't reach PSRAM. The
  // big ones go there anyway: LVGL's pool, MIDI's buffers, TLS's)
  // and TLS's buffers (about 40 KB a connection): with Bluetooth on, the internal RAM can't spare them
  mbedtls_platform_set_calloc_free(
      [](size_t n, size_t size) {
        void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM);
        return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL);
      },
      free);
#ifdef LED_RGB
  rgbLedWrite(LED_RGB, 0, 0, 0);
#endif
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
  sdOk = SD_MMC.begin("/sdcard", false);
  WiFi.mode(WIFI_OFF);
  // a computer on the USB-C port (it sends start-of-frames): stay its serial port. Else: the USB host
  // (board::usbBegin), and the log goes to the SD card
  bool test = hostTest == HOST_TEST;
  hostTest = 0;
  for (uint32_t t = millis(); !test && millis() - t < 1500 && !computer; delay(10)) {
    computer = HWCDC::isPlugged();
  }
  if (!computer && sdOk) {
    mkdir("/sdcard/fm1buddy", 0777);
    if (FILE* f = fopen(LOG_FILE, "w")) {
      fclose(f);
      logToSd = true;
    }
  }
  if (test) {
    hostTestUntil = millis() + 20000;
  }
  hal::log("fm1buddy: sd %s, usb %s%s, last reset: %d", sdOk ? "mounted" : "none",
           computer ? "serial (a computer)" : "host", test ? " (a test: 20 s)" : "", (int)esp_reset_reason());
}

// the USB port back to the serial port: a restart (esp_restart) keeps the host's choice, and a computer on it
// then wouldn't find the board until it's reset by its button
static void phyToSerial() {
  CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL_M | RTC_CNTL_SW_USB_PHY_SEL_M);
}

void board::usbBegin() {
  if (computer) {
    return;
  }
  esp_register_shutdown_handler(phyToSerial);
  hal::log("usb: serial off");
  Serial.end();
  usbHostBegin();
}

void board::loop() {
  if (hostTestUntil && (int32_t)(millis() - hostTestUntil) >= 0) {
    hal::log("usb: the test is over: restarting");
    esp_restart();
  }
}

void board::hostTestStart() {
  hostTest = HOST_TEST;
  esp_restart();
}

void board::printLog() {
  FILE* f = fopen(LOG_FILE, "r");
  if (!f) {
    Serial.println("no log");
    return;
  }
  char line[200];
  while (fgets(line, sizeof line, f)) {
    Serial.print(line);
  }
  fclose(f);
}

uint32_t hal::millis() {
  return ::millis();
}

void hal::log(const char* fmt, ...) {
  char buf[160];
  va_list a;
  va_start(a, fmt);
  vsnprintf(buf, sizeof buf, fmt, a);
  va_end(a);
  if (!logToSd) {
    Serial.println(buf);
  } else if (FILE* f = fopen(LOG_FILE, "a")) {
    fprintf(f, "%7.1f %s\n", ::millis() / 1000.0, buf);
    fclose(f);
  }
}

const char* hal::sd() {
  return sdOk ? "/sdcard" : 0;
}

bool hal::sdSpace(uint64_t& total, uint64_t& used) {
  if (!sdOk) {
    return false;
  }
  total = SD_MMC.totalBytes();
  used = SD_MMC.usedBytes();
  return true;
}

bool hal::load(const char* key, void* p, size_t n) {
  Preferences prefs;
  prefs.begin("fm1buddy", false);
  bool ok = prefs.getBytesLength(key) == n && prefs.getBytes(key, p, n) == n;
  prefs.end();
  return ok;
}

void hal::save(const char* key, const void* p, size_t n) {
  Preferences prefs;
  prefs.begin("fm1buddy", false);
  prefs.putBytes(key, p, n);
  prefs.end();
}

void hal::backlight(uint8_t percent) {
  static bool attached;
  if (!attached) {
    ledcAttach(TFT_BL, 5000, 8);
    attached = true;
  }
  ledcWrite(TFT_BL, percent * 255 / 100);
}

// --------------------------------------------------------------------------------------------- Wi-Fi ---
bool hal::wifiScan(std::vector<Ap>& out) {
  board::blePause(true);  // both don't fit
  radio = true;
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  out.clear();
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) {
      continue;  // hidden
    }
    auto a = std::find_if(out.begin(), out.end(), [&](const Ap& x) { return x.ssid == s.c_str(); });
    if (a == out.end()) {
      out.push_back({s.c_str(), (int)WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
    } else if (WiFi.RSSI(i) > a->rssi) {
      a->rssi = WiFi.RSSI(i);
    }
  }
  WiFi.scanDelete();
  std::sort(out.begin(), out.end(), [](const Ap& a, const Ap& b) { return a.rssi > b.rssi; });
  return n >= 0;
}

bool hal::wifiJoin(const char* ssid, const char* pass, std::string& err) {
  hal::log("wifi: %u KB internal RAM free", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
  board::blePause(true);
  radio = true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  for (uint32_t t = ::millis(); ::millis() - t < 15000; delay(50)) {
    wl_status_t s = WiFi.status();
    if (s == WL_CONNECTED) {
      wifiUp = true;
      return true;
    }
    if (s == WL_CONNECT_FAILED || s == WL_NO_SSID_AVAIL) {
      break;
    }
  }
  wl_status_t s = WiFi.status();
  err = std::string(s == WL_NO_SSID_AVAIL ? "Can't find " : "Can't join ") + ssid +
        (s == WL_NO_SSID_AVAIL ? "." : ": check the password.");
  wifiOff();
  return false;
}

void hal::wifiOff() {
  wifiUp = false;
  if (!radio) {
    return;
  }
  radio = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  board::blePause(false);
}

bool hal::wifiOn() {
  return wifiUp;
}

// ---------------------------------------------------------------------------------------------- HTTPS ---
static esp_err_t onEvent(esp_http_client_event_t* e) {
  if (e->event_id == HTTP_EVENT_ON_HEADER && !strcasecmp(e->header_key, "ETag")) {
    *(std::string*)e->user_data = e->header_value;
  }
  return ESP_OK;
}

int hal::get(const char* url, const char* path, std::string* etag, void (*progress)(uint32_t, uint32_t),
             std::string& err) {
  std::string tag;
  esp_http_client_config_t c = {};
  c.url = url;
  c.event_handler = onEvent;
  c.user_data = &tag;
  c.crt_bundle_attach = esp_crt_bundle_attach;
  c.timeout_ms = 15000;
  c.buffer_size = 4096;
  c.user_agent = "fm1buddy";
  esp_http_client_handle_t h = esp_http_client_init(&c);
  if (etag && !etag->empty()) {
    esp_http_client_set_header(h, "If-None-Match", etag->c_str());
  }
  int status = -1;
  for (int hops = 0; hops < 5; hops++) {
    esp_err_t r = esp_http_client_open(h, 0);
    if (r != ESP_OK) {
      err = std::string("Can't reach it (") + esp_err_to_name(r) + ").";
      break;
    }
    int64_t len = esp_http_client_fetch_headers(h);
    status = esp_http_client_get_status_code(h);
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      esp_http_client_set_redirection(h);
      esp_http_client_close(h);
      status = -1;
      continue;
    }
    if (status != 200) {
      break;
    }
    std::string part = std::string(path) + ".part";
    FILE* f = fopen(part.c_str(), "wb");
    if (!f) {
      err = "Can't write to the SD card.";
      status = -1;
      break;
    }
    static char* buf = (char*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    uint32_t got = 0;
    bool ok = true;
    for (;;) {
      int n = esp_http_client_read(h, buf, 4096);
      if (n < 0) {
        err = "The download stopped part way.";
        ok = false;
        break;
      }
      if (n == 0) {
        if (!esp_http_client_is_complete_data_received(h)) {
          err = "The download stopped part way.";
          ok = false;
        }
        break;
      }
      if (fwrite(buf, 1, n, f) != (size_t)n) {
        err = "Can't write to the SD card (is it full?).";
        ok = false;
        break;
      }
      got += n;
      if (progress) {
        progress(got, len > 0 ? (uint32_t)len : 0);
      }
    }
    fclose(f);
    remove(path);
    if (!ok || rename(part.c_str(), path)) {
      if (ok) {
        err = "Can't rename it on the SD card.";
      }
      remove(part.c_str());
      status = -1;
    }
    break;
  }
  esp_http_client_close(h);
  esp_http_client_cleanup(h);
  if (status == 200 && etag) {
    *etag = tag;
  }
  return status;
}

bool hal::sha256(const char* path, char hex[65]) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    return false;
  }
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  static uint8_t* buf = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
  for (size_t n; (n = fread(buf, 1, 4096, f));) {
    mbedtls_sha256_update(&ctx, buf, n);
  }
  fclose(f);
  uint8_t sum[32];
  mbedtls_sha256_finish(&ctx, sum);
  mbedtls_sha256_free(&ctx);
  for (int i = 0; i < 32; i++) {
    sprintf(hex + i * 2, "%02x", sum[i]);
  }
  return true;
}

// ----------------------------------------------------------------------------------------------- jobs ---
struct Spawn {
  void (*fn)(void*);
  void* arg;
};

static void trampoline(void* p) {
  Spawn s = *(Spawn*)p;
  delete (Spawn*)p;
  s.fn(s.arg);
  vTaskDelete(0);
}

void hal::spawn(void (*fn)(void*), void* arg) {
  xTaskCreatePinnedToCore(trampoline, "job", 16384, new Spawn{fn, arg}, 1, 0, 0);
}
