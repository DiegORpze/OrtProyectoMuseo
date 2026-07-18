#include "esp_camera.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <quirc.h>
#include <semphr.h>

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

// PSRAM buffer for grayscale scan data
static uint8_t *scan_buf = nullptr;

struct quirc *qr = nullptr;

// Binary semaphore: Core 1 gives → Core 0 takes
static SemaphoreHandle_t frame_sem = nullptr;

// Shared result data
static volatile bool qr_detected = false;
static char qr_payload[64] = "";
static String last_qr_payload = "";
static unsigned long last_qr_time = 0;
static const unsigned long QR_COOLDOWN_MS = 3000;
static const unsigned long RESULT_DURATION_MS = 4000;

// Result overlay state
static bool showing_result = false;
static unsigned long result_until = 0;

// Scan throttle: only convert + scan every N frames
static const int FRAMES_BETWEEN_SCANS = 30;
static int frame_counter = 0;

// Task handle for Core 0
static TaskHandle_t scanner_task = nullptr;

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
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(false);

  scan_buf = (uint8_t *)ps_malloc(320 * 240);
  if (!scan_buf) {
    Serial.println("scan_buf alloc failed!");
  }

  qr = quirc_new();
  if (!qr) {
    Serial.println("quirc alloc failed!");
  } else {
    quirc_resize(qr, 320, 240);
  }

  // Binary semaphore for frame delivery (Core 1 → Core 0)
  frame_sem = xSemaphoreCreateBinary();
  if (!frame_sem) {
    Serial.println("semaphore failed!");
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
  config.grab_mode = CAMERA_GRAB_LATEST;

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

  xTaskCreatePinnedToCore(scannerTask, "Scanner", 10000, NULL, 1, &scanner_task, 0);

  Serial.println("Ready");
}

// ================================================================
// CORE 1 — Video + UI loop
//
// Strict ordering per frame:
//   1. esp_camera_fb_get()     → get frame
//   2. pushImage()              → display it (fully completes)
//   3. esp_camera_fb_return()   → give buffer back to camera
//   4. (on throttle): convert + give semaphore to Core 0
//
// The camera buffer is NEVER held during any scan or display work.
// ================================================================
void loop() {
  unsigned long now = millis();

  // ---- RESULT STATE: wait out the timeout, drain camera buffers ----
  if (showing_result) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);

    if (now >= result_until) {
      showing_result = false;
      last_qr_payload = "";
      tft.fillScreen(TFT_BLACK);
    }
    return;
  }

  // ---- LIVE VIDEO ----
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  // Display frame — this is the only display operation.
  // pushImage blocks until the entire frame is transferred over SPI.
  tft.pushImage(0, 0, fb->width, fb->height, (uint16_t *)fb->buf);

  // Return the camera buffer IMMEDIATELY after pushImage.
  // The camera can now reuse/recycle this buffer for the next capture.
  esp_camera_fb_return(fb);
  fb = nullptr;

  // ---- THROTTLED SCAN: only convert a frame every N video frames ----
  // This prevents scan work from stealing CPU time from video display.
  frame_counter++;
  if (frame_counter >= FRAMES_BETWEEN_SCANS) {
    frame_counter = 0;

    // Convert RGB565 → grayscale into scan_buf
    // We read from the frame we already displayed (fb was returned above).
    // This conversion is independent of the camera — camera is already free.
    camera_fb_t *tmp = esp_camera_fb_get();
    if (tmp) {
      uint8_t *src = tmp->buf;
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
      esp_camera_fb_return(tmp);
    }

    // Signal Core 0 to run QR detection on the grayscale buffer
    if (frame_sem) {
      xSemaphoreGive(frame_sem);
    }
  }

  // ---- CHECK FOR QR RESULT FROM CORE 0 ----
  if (qr_detected) {
    qr_detected = false;

    unsigned long elapsed = now - last_qr_time;
    if (strcmp(qr_payload, last_qr_payload.c_str()) != 0 ||
        elapsed >= QR_COOLDOWN_MS) {

      last_qr_payload = String(qr_payload);
      last_qr_time = now;

      Serial.print("QR: ");
      Serial.println(qr_payload);

      const char *name = lookupPainting(qr_payload);
      drawResultScreen(name ? name : qr_payload, name != nullptr);
      showing_result = true;
      result_until = now + RESULT_DURATION_MS;
    }
  }
}

// ================================================================
// CORE 0 — QR scanner task
//
// Waits on a semaphore before processing each frame.
// Runs grayscale → quirc decode, then signals Core 1 via qr_detected.
// Completely decoupled from camera and display — only touches scan_buf.
// ================================================================
void scannerTask(void *pvParameters) {
  for (;;) {
    // Wait for Core 1 to give us a frame to process
    if (frame_sem) {
      BaseType_t got = xSemaphoreTake(frame_sem, portMAX_DELAY);
      if (got != pdTRUE) continue;
    }

    // QR detection on scan_buf (which is in PSRAM, independent of camera)
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
        // Strip trailing control characters
        for (int i = 0; i < len; i++) {
          if (tmp[i] < 32) { tmp[i] = '\0'; break; }
        }
        strncpy(qr_payload, tmp, 63);
        qr_payload[63] = '\0';
        qr_detected = true;
      }
    }
  }
}
