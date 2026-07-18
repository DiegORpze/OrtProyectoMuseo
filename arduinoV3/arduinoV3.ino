#include "esp_camera.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <quirc.h>

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

TFT_eSPI tft = TFT_eSPI();

// Two buffers so camera and scan never fight
static uint8_t *cam_buf[2] = { nullptr, nullptr };
static uint8_t *scan_buf = nullptr;
static int cam_write_idx = 0;
static int cam_read_idx = 0;

struct quirc *qr = nullptr;

bool showing_result = false;
unsigned long result_until = 0;
const unsigned long RESULT_DURATION_MS = 4000;

const char *lookupPainting(const char *payload) {
  if (strcmp(payload, "MonaLisa") == 0)    return "Mona Lisa";
  if (strcmp(payload, "StarryNight") == 0)  return "Starry Night";
  if (strcmp(payload, "TheScream") == 0)    return "The Scream";
  return nullptr;
}

void drawResultScreen(const char *painting_name, bool known) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(painting_name, 160, 80, 4);
  tft.setTextColor(known ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
  tft.drawString(known ? "Painting Identified" : "Unknown QR Code", 160, 125, 2);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Returning to live video...", 160, 185, 2);
  tft.setTextDatum(TL_DATUM);
}

void setup() {
  Serial.begin(230400);
  delay(1000);

  pinMode(12, OUTPUT);
  digitalWrite(12, LOW);
  delay(50);
  digitalWrite(12, HIGH);
  delay(150);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(false);

  scan_buf = (uint8_t *)ps_malloc(320 * 240);
  if (!scan_buf) {
    Serial.println("Failed to allocate scan buffer!");
  }

  qr = quirc_new();
  if (!qr) {
    Serial.println("Failed to allocate quirc!");
  } else {
    quirc_resize(qr, 320, 240);
  }

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = FRAMESIZE_QVGA;
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  Serial.println("Initializing camera...");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    tft.fillScreen(TFT_RED);
    tft.drawString("Camera Error", 10, 10, 2);
    Serial.printf("Error: 0x%x\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
  }

  Serial.println("Ready");
}

void loop() {
  unsigned long now = millis();

  // ---- RESULT STATE: just wait, drain camera frames to keep buffer healthy ----
  if (showing_result) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);

    if (now >= result_until) {
      showing_result = false;
      tft.fillScreen(TFT_BLACK);
    }
    return;
  }

  // ---- LIVE VIDEO + QR SCAN ----

  // Get a camera frame
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  // Display it immediately
  tft.pushImage(0, 0, fb->width, fb->height, (uint16_t *)fb->buf);

  // Convert the frame to grayscale for QR scanning
  // This is done in-place using the camera frame buffer BEFORE returning it,
  // so we don't need extra RAM. quirc will get its own copy after.
  uint8_t *src = fb->buf;
  for (int i = 0; i < 320 * 240; i++) {
    uint16_t pixel = (src[i * 2] << 8) | src[i * 2 + 1];
    uint8_t r5 = (pixel >> 11) & 0x1F;
    uint8_t g6 = (pixel >> 5)  & 0x3F;
    uint8_t b5 =  pixel        & 0x1F;
    uint8_t r = (r5 << 3) | (r5 >> 2);
    uint8_t g = (g6 << 2) | (g6 >> 4);
    uint8_t b = (b5 << 3) | (b5 >> 2);
    scan_buf[i] = (r * 77 + g * 150 + b * 29) >> 8;
  }

  // Return the camera frame immediately after conversion is done
  // The display DMA was already scheduled by pushImage above
  esp_camera_fb_return(fb);

  // Now run QR detection on the grayscale data (camera is free to grab next frame)
  int w = 0, h = 0;
  uint8_t *qbuf = quirc_begin(qr, &w, &h);
  if (qbuf) {
    memcpy(qbuf, scan_buf, 320 * 240);
  }
  quirc_end(qr);

  if (quirc_count(qr) > 0) {
    struct quirc_code code;
    struct quirc_data data;
    quirc_extract(qr, 0, &code);
    if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
      int len = data.payload_len;
      if (len > 63) len = 63;
      char tmp[64];
      memcpy(tmp, data.payload, len);
      tmp[len] = '\0';
      for (int i = 0; i < len; i++) {
        if (tmp[i] < 32) { tmp[i] = '\0'; break; }
      }

      Serial.print("QR: ");
      Serial.println(tmp);

      const char *name = lookupPainting(tmp);
      drawResultScreen(name ? name : tmp, name != nullptr);
      showing_result = true;
      result_until = now + RESULT_DURATION_MS;
    }
  }
}
