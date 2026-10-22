// hal.h on the CYD-S3: the SD card (4-bit SD_MMC, at /sdcard for stdio), NVS for the settings, Wi-Fi, and HTTPS
// with ESP-IDF's certificate bundle
#include <Arduino.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <mbedtls/sha256.h>

#include <algorithm>

#include "board.h"
#include "hal.h"

static bool sdOk;
static volatile bool wifiUp, radio;

void board::begin() {
  // the app's lists and strings go to PSRAM: the internal RAM is for Wi-Fi (and soon USB and Bluetooth)
  heap_caps_malloc_extmem_enable(256);
#ifdef LED_RGB
  rgbLedWrite(LED_RGB, 0, 0, 0);
#endif
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
  sdOk = SD_MMC.begin("/sdcard", false);
  Serial.printf("sd: %s\n", sdOk ? "mounted" : "none");
  WiFi.mode(WIFI_OFF);
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
  Serial.println(buf);
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
    static char buf[4096];
    uint32_t got = 0;
    bool ok = true;
    for (;;) {
      int n = esp_http_client_read(h, buf, sizeof buf);
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
  static uint8_t buf[4096];
  for (size_t n; (n = fread(buf, 1, sizeof buf, f));) {
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
