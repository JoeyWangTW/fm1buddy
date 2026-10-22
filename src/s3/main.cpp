// fm1buddy on the CYD-S3 (Freenove ESP32-S3 2.8", FNK0104B): the ILI9341 screen (TFT_eSPI) and the FT6336 touch
// panel for LVGL, and the app (src/app)
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <lvgl.h>

#include "board.h"
#include "ui.h"

#define W 320
#define H 240
#define FT6336 0x38

static TFT_eSPI tft;

static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  uint32_t w = lv_area_get_width(a), h = lv_area_get_height(a);
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.pushColors((uint16_t*)px, w * h, true);
  tft.endWrite();
  lv_display_flush_ready(d);
}

static void touchBegin() {
  pinMode(TOUCH_INT, INPUT);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, 0);
  delay(10);
  digitalWrite(TOUCH_RST, 1);
  delay(300);
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
}

// a tap from the serial console, until then
static int16_t tapX, tapY;
static uint32_t tapUntil;

static void readTouch(lv_indev_t*, lv_indev_data_t* data) {
  static int16_t lx, ly;
  if (tapUntil && (int32_t)(tapUntil - millis()) > 0) {
    data->point.x = tapX;
    data->point.y = tapY;
    data->state = LV_INDEV_STATE_PRESSED;
    return;
  }
  tapUntil = 0;
  uint8_t r[5];  // TD_STATUS, then the first point: XH XL YH YL
  data->state = LV_INDEV_STATE_RELEASED;
  Wire.beginTransmission(FT6336);
  Wire.write(0x02);
  if (!Wire.endTransmission(false) && Wire.requestFrom(FT6336, 5) == 5) {
    for (uint8_t& b : r) {
      b = Wire.read();
    }
    uint8_t n = r[0] & 0x0F;
    if (n >= 1 && n <= 2) {
      // the panel is portrait: turned as the screen is (rotation 1)
      int px = (r[1] & 0x0F) << 8 | r[2], py = (r[3] & 0x0F) << 8 | r[4];
      lx = constrain(py, 0, W - 1);
      ly = constrain(H - 1 - px, 0, H - 1);
      data->state = LV_INDEV_STATE_PRESSED;
    }
  }
  data->point.x = lx;
  data->point.y = ly;
}

static uint32_t tick() {
  return millis();
}

void setup() {
  Serial.begin(115200);
  board::begin();

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  touchBegin();

  lv_init();
  lv_tick_set_cb(tick);
  lv_log_register_print_cb([](lv_log_level_t, const char* s) { Serial.print(s); });
  lv_display_t* disp = lv_display_create(W, H);
  const size_t n = W * 40 * 2;
  void* a = heap_caps_malloc(n, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void* b = heap_caps_malloc(n, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_display_set_buffers(disp, a, b, n, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush);
  lv_indev_t* in = lv_indev_create();
  lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(in, readTouch);

  ui::begin();
  Serial.printf("fm1buddy: up, %u KB internal RAM free, %u KB PSRAM free\n",
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(ESP.getFreePsram() / 1024));
}

// the serial console (tools/s3.py): "shot" sends the screen (SHOT w h, then w*h RGB565 pixels, little-endian),
// "tap x y" touches it for a moment
static void console() {
  static char line[32];
  static uint8_t n;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (n < sizeof line - 1) {
        line[n++] = c;
      }
      continue;
    }
    line[n] = 0;
    n = 0;
    int x, y;
    if (!strcmp(line, "shot")) {
      lv_draw_buf_t* d = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
      if (d) {
        Serial.printf("SHOT %d %d\n", W, H);
        for (int r = 0; r < H; r++) {
          Serial.write(d->data + r * d->header.stride, W * 2);
        }
        Serial.flush();
        lv_draw_buf_destroy(d);
      } else {
        Serial.println("NOSHOT");
      }
    } else if (sscanf(line, "tap %d %d", &x, &y) == 2) {
      tapX = x;
      tapY = y;
      tapUntil = millis() + 80;
    }
  }
}

void loop() {
  console();
  uint32_t wait = lv_timer_handler();
  delay(wait > 5 ? 5 : wait);
}
