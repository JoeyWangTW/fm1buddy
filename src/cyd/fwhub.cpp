#include "fwhub.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#if MBEDTLS_VERSION_MAJOR >= 3  // the S3's newer ESP-IDF: the _ret names are gone
#define mbedtls_sha256_starts_ret mbedtls_sha256_starts
#define mbedtls_sha256_update_ret mbedtls_sha256_update
#define mbedtls_sha256_finish_ret mbedtls_sha256_finish
#endif

#include "link.h"
#include "roots.h"

HubPkg fwhub::list[HUB_MAX];
int fwhub::count;
static char err[96];

const char* HubPkg::file() const {
  const char* s = strrchr(pkg, '/');
  return s ? s + 1 : pkg;
}

const char* fwhub::join(const char* ssid, const char* pass, void (*idle)()) {
  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid) {
    return 0;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  for (uint32_t t = millis(); millis() - t < 20000;) {
    wl_status_t s = WiFi.status();
    if (s == WL_CONNECTED) {
      return 0;
    }
    if (s == WL_CONNECT_FAILED) {
      break;
    }
    idle();
    delay(20);
  }
  WiFi.disconnect(true);
  snprintf(err, sizeof err, "Can't join %s: check the password.", ssid);
  return err;
}

// the model a product is for: "FM-1" from "FM-1_974"
static bool forModel(const char* product, const char* model) {
  const char* us = strchr(product, '_');
  size_t n = us ? us - product : strlen(product);
  return strlen(model) == n && !strncmp(product, model, n);
}

static bool add(JsonVariantConst e, const char* name, const char* author, const char* model) {
  if (fwhub::count >= HUB_MAX) {
    return false;
  }
  const char* pkg = e["pkg"];
  const char* product = e["product"];
  const char* sha = e["sha256"];
  uint32_t size = e["size"] | 0;
  if (!pkg || !product || !sha || strlen(sha) != 64 || !size || !forModel(product, model) || strstr(pkg, "..")) {
    return false;
  }
  HubPkg& p = fwhub::list[fwhub::count++];
  strlcpy(p.name, name, sizeof p.name);
  strlcpy(p.author, author, sizeof p.author);
  strlcpy(p.version, e["version"] | "", sizeof p.version);
  strlcpy(p.pkg, pkg, sizeof p.pkg);
  strlcpy(p.product, product, sizeof p.product);
  strlcpy(p.sha256, sha, sizeof p.sha256);
  p.size = size;
  p.beta = e["beta"] | false;
  return true;
}

const char* fwhub::fetch(const Fm1Id* dev) {
  count = 0;
  WiFiClientSecure tls;
  tls.setCACert(ROOTS);
  HTTPClient http;
  http.useHTTP10(true);  // no chunks: the JSON straight from the socket
  http.setUserAgent("fm1buddy");
  http.setTimeout(15000);
  if (!http.begin(tls, HUB_SITE "catalog.json")) {
    return "Can't reach thefirmwarehub.com.";
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(err, sizeof err, "The catalogue didn't come (%s).", code < 0 ? http.errorToString(code).c_str() : String(code).c_str());
    http.end();
    return err;
  }
  JsonDocument filter;
  for (const char* k : {"name", "author", "version", "pkg", "product", "size", "sha256", "beta"}) {
    filter["firmwares"][0][k] = true;
  }
  for (const char* k : {"pkg", "product", "size", "sha256"}) {
    filter["stock"][k] = true;
  }
  JsonDocument doc;
  Stream& body = http.getStream();
  body.setTimeout(15000);  // (Stream's, in ms) a slow network pauses longer than the second it waits by default
  DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  http.end();
  if (e) {
    snprintf(err, sizeof err, "The catalogue didn't read (%s).", e.c_str());
    return err;
  }
  const char* model = dev && !dev->ota() ? dev->model : "FM-1";
  add(doc["stock"], "M-VAVE stock", "M-VAVE", model);
  int first = count;  // the rest by name
  for (JsonVariantConst f : doc["firmwares"].as<JsonArrayConst>()) {
    add(f, f["name"] | "?", f["author"] | "", model);
  }
  std::sort(list + first, list + count, [](const HubPkg& a, const HubPkg& b) {
    return strcasecmp(a.name, b.name) < 0;
  });
  if (!count) {
    snprintf(err, sizeof err, "Nothing in the catalogue for %s.", model);
    return err;
  }
  return 0;
}

const char* fwhub::download(const HubPkg& p, fs::FS& fs, const char* path, void (*progress)(uint32_t, uint32_t)) {
  WiFiClientSecure tls;
  tls.setCACert(ROOTS);
  HTTPClient http;
  http.setTimeout(15000);
  http.setUserAgent("fm1buddy");
  String url = String(HUB_SITE) + p.pkg;
  if (!http.begin(tls, url)) {
    return "Can't reach thefirmwarehub.com.";
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(err, sizeof err, "The download didn't start (%s).", code < 0 ? http.errorToString(code).c_str() : String(code).c_str());
    http.end();
    return err;
  }
  String part = String(path) + ".part";
  File f = fs.open(part, "w");
  if (!f) {
    http.end();
    return "Can't write to the SD card.";
  }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0);
  WiFiClient* s = http.getStreamPtr();
  int left = http.getSize();  // -1: chunked, unknown
  uint32_t got = 0, last = millis();
  static uint8_t buf[4096];
  bool ok = true;
  while (http.connected() && (left > 0 || left == -1) && got <= p.size) {
    size_t n = s->available();
    if (!n) {
      if (millis() - last > 15000) {
        break;
      }
      hub.poll();
      delay(2);
      continue;
    }
    n = s->readBytes(buf, n > sizeof buf ? sizeof buf : n);
    if (f.write(buf, n) != n) {
      ok = false;
      break;
    }
    mbedtls_sha256_update_ret(&sha, buf, n);
    got += n;
    if (left > 0) {
      left -= n;
    }
    last = millis();
    progress(got, p.size);
    hub.poll();
  }
  http.end();
  f.close();
  uint8_t sum[32];
  mbedtls_sha256_finish_ret(&sha, sum);
  mbedtls_sha256_free(&sha);
  char hex[65];
  for (int i = 0; i < 32; i++) {
    sprintf(hex + i * 2, "%02x", sum[i]);
  }
  const char* why = !ok                         ? "Can't write to the SD card (is it full?)."
                    : got != p.size             ? "The download stopped part way."
                    : strcasecmp(hex, p.sha256) ? "The download isn't what the catalogue says (sha256): not kept."
                                                : 0;
  if (!why) {
    Pkg pkg;  // its own header names the same device
    if (!pkg.open(fs, part.c_str()) || strcmp(pkg.product, p.product)) {
      why = "The package isn't for the device the catalogue says: not kept.";
    }
    pkg.f.close();
  }
  if (why) {
    fs.remove(part);
    return why;
  }
  fs.remove(path);
  return fs.rename(part, path) ? 0 : "Can't rename it on the SD card.";
}
