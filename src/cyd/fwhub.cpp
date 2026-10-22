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

static bool read(JsonVariantConst e, const char* name, const char* author, const char* model, HubPkg& p) {
  const char* pkg = e["pkg"];
  const char* product = e["product"];
  const char* sha = e["sha256"];
  uint32_t size = e["size"] | 0;
  if (!pkg || !product || !sha || strlen(sha) != 64 || !size || !forModel(product, model) || strstr(pkg, "..")) {
    return false;
  }
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

// the response's body into f, as it comes (MIDI keeps going): its length, or -1 if f couldn't take it
static int32_t pull(HTTPClient& http, File& f, uint32_t max, mbedtls_sha256_context* sha,
                    void (*progress)(uint32_t, uint32_t), uint32_t total) {
  WiFiClient* s = http.getStreamPtr();
  int left = http.getSize();  // -1: chunked, unknown
  uint32_t got = 0, last = millis();
  static uint8_t buf[4096];
  while (http.connected() && (left > 0 || left == -1) && got <= max) {
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
      return -1;
    }
    if (sha) {
      mbedtls_sha256_update_ret(sha, buf, n);
    }
    got += n;
    if (left > 0) {
      left -= n;
    }
    last = millis();
    if (progress) {
      progress(got, total);
    }
    hub.poll();
  }
  return got;
}

// the catalogue onto the card, unless the copy there is the latest (its ETag): error, or 0
static const char* getCatalog(fs::FS& fs) {
  char etag[96] = "";
  File t = fs.open(HUB_ETAG);
  if (t && fs.exists(HUB_CATALOG)) {
    etag[t.readBytes(etag, sizeof etag - 1)] = 0;
  }
  t.close();
  WiFiClientSecure tls;
  tls.setCACert(ROOTS);
  HTTPClient http;
  http.setTimeout(15000);
  http.setUserAgent("fm1buddy");
  const char* keep[] = {"ETag"};
  http.collectHeaders(keep, 1);
  if (!http.begin(tls, HUB_SITE "catalog.json")) {
    return "Can't reach thefirmwarehub.com.";
  }
  if (etag[0]) {
    http.addHeader("If-None-Match", etag);
  }
  int code = http.GET();
  if (code == HTTP_CODE_NOT_MODIFIED) {
    http.end();
    return 0;
  }
  if (code != HTTP_CODE_OK) {
    snprintf(err, sizeof err, "The catalogue didn't come (%s).", code < 0 ? http.errorToString(code).c_str() : String(code).c_str());
    http.end();
    return err;
  }
  String tag = http.header("ETag");
  int size = http.getSize();
  File f = fs.open(HUB_CATALOG ".part", "w");
  if (!f) {
    http.end();
    return "Can't write to the SD card.";
  }
  int32_t got = pull(http, f, 4 << 20, 0, 0, 0);
  http.end();
  f.close();
  if (got < 0 || (size > 0 && got != size)) {
    fs.remove(HUB_CATALOG ".part");
    return got < 0 ? "Can't write to the SD card (is it full?)." : "The catalogue stopped part way.";
  }
  fs.remove(HUB_CATALOG);
  fs.remove(HUB_ETAG);
  if (!fs.rename(HUB_CATALOG ".part", HUB_CATALOG)) {
    return "Can't rename it on the SD card.";
  }
  t = fs.open(HUB_ETAG, "w");
  t.print(tag);
  t.close();
  return 0;
}

static int skipSpace(Stream& s) {
  while (s.peek() == ' ' || s.peek() == '\n' || s.peek() == '\r' || s.peek() == '\t') {
    s.read();
  }
  return s.peek();
}

// the catalogue from the card, one entry at a time (all of it doesn't fit in memory): error, or 0
static const char* readCatalog(fs::FS& fs, const char* model) {
  File f = fs.open(HUB_CATALOG);
  if (!f || skipSpace(f) != '{') {
    return "The catalogue didn't read.";
  }
  f.read();
  JsonDocument filter, stockFilter, none, key, e;
  for (const char* k : {"name", "author", "version", "pkg", "product", "size", "sha256", "beta"}) {
    filter[k] = true;
  }
  for (const char* k : {"pkg", "product", "size", "sha256"}) {
    stockFilter[k] = true;
  }
  none.set(false);  // skip the value
  HubPkg stock;
  bool gotStock = false;
  DeserializationError bad = DeserializationError::Ok;
  while (!bad && skipSpace(f) == '"') {  // each key of the top level
    bad = deserializeJson(key, f);
    if (bad || skipSpace(f) != ':') {
      break;
    }
    f.read();
    if (key.as<String>() == "firmwares" && skipSpace(f) == '[') {
      f.read();
      while (skipSpace(f) != ']' && f.available()) {
        bad = deserializeJson(e, f, DeserializationOption::Filter(filter));
        if (bad) {
          break;
        }
        if (fwhub::count < HUB_MAX && read(e, e["name"] | "?", e["author"] | "", model, fwhub::list[fwhub::count])) {
          fwhub::count++;
        }
        if (skipSpace(f) == ',') {
          f.read();
        }
      }
      f.read();  // ]
    } else if (key.as<String>() == "stock") {
      bad = deserializeJson(e, f, DeserializationOption::Filter(stockFilter));
      gotStock = !bad && read(e, "M-VAVE stock", "M-VAVE", model, stock);
    } else {
      bad = deserializeJson(e, f, DeserializationOption::Filter(none));
    }
    if (skipSpace(f) == ',') {
      f.read();
    }
  }
  f.close();
  if (bad) {
    snprintf(err, sizeof err, "The catalogue didn't read (%s).", bad.c_str());
    return err;
  }
  std::sort(fwhub::list, fwhub::list + fwhub::count, [](const HubPkg& a, const HubPkg& b) {
    return strcasecmp(a.name, b.name) < 0;
  });
  if (gotStock) {  // M-VAVE's own first
    if (fwhub::count == HUB_MAX) {
      fwhub::count--;
    }
    memmove(fwhub::list + 1, fwhub::list, fwhub::count * sizeof(HubPkg));
    fwhub::list[0] = stock;
    fwhub::count++;
  }
  return 0;
}

const char* fwhub::fetch(const Fm1Id* dev, fs::FS& fs) {
  count = 0;
  const char* why = getCatalog(fs);
  if (why && !fs.exists(HUB_CATALOG)) {
    return why;  // and no copy from before to use
  }
  const char* model = dev && !dev->ota() ? dev->model : "FM-1";
  why = readCatalog(fs, model);
  if (why) {
    return why;
  }
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
  int32_t got = pull(http, f, p.size, &sha, progress, p.size);
  bool ok = got >= 0;
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
                    : (uint32_t)got != p.size   ? "The download stopped part way."
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
