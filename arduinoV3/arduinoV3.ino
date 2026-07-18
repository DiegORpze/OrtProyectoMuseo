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

struct quirc *qr = nullptr;

uint8_t *grayscale_buf = nullptr;

// Shared result
volatile bool qr_detected = false;
char qr_payload[64] = "";

const unsigned long RESULT_DURATION_MS = 5000;

TaskHandle_t Task0;

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

  if (ESP.getPsramSize() > 0) {
    grayscale_buf = (uint8_t *)ps_malloc(320 * 240);
  } else {
    grayscale_buf = (uint8_t *)malloc(320 * 240);
  }

  if (!grayscale_buf) {
    Serial.println("Failed to allocate grayscale buffer!");
  }

  qr = quirc_new();
  if (!qr) {
    Serial.println("Failed to allocate quirc memory");
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
  config.grab_mode = CAMERA_GRAB_LATEST;

  Serial.println("Initializing camera...");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    tft.fillScreen(TFT_RED);
    tft.drawString("Cam Error", 10, 10, 2);
    Serial.printf("Error: 0x%x\n", err);
    return;
  }

  xTaskCreatePinnedToCore(scannerTask, "Scanner", 10000, NULL, 1, &Task0, 0);
  Serial.println("Ready");
}

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
  tft.drawString(painting_name, 160, 90, 4);
  tft.setTextColor(known ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
  tft.drawString(known ? "Painting Identified" : "Unknown QR Code", 160, 140, 2);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Returning to live video...", 160, 200, 2);
  tft.setTextDatum(TL_DATUM);
}

// Convert RGB565 camera frame to grayscale in grayscale_buf
void convertToGrayscale(camera_fb_t *fb) {
  uint8_t *src = fb->buf;
  for (int i = 0; i < 320 * 240; i++) {
    uint16_t pixel = (src[i*2] << 8) | src[i*2+1];
    uint8_t r5 = (pixel >> 11) & 0x1F;
    uint8_t g6 = (pixel >> 5)  & 0x3F;
    uint8_t b5 =  pixel        & 0x1F;
    uint8_t r = (r5 << 3) | (r5 >> 2);
    uint8_t g = (g6 << 2) | (g6 >> 4);
    uint8_t b = (b5 << 3) | (b5 >> 2);
    grayscale_buf[i] = (r * 77 + g * 150 + b * 29) >> 8;
  }
}

// Drain all pending camera frames to empty the buffer pool
void drainCameraBuffers() {
  for (int i = 0; i < 3; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
  }
}

// ------------------- CORE 1: VIDEO & RESULT LOOP -------------------
void loop() {
  unsigned long now = millis();

  // ---- QR RESULT STATE ----
  // Step 4: black screen with QR text, wait for timeout
  if (qr_detected) {
    // Draw result screen once
    const char *name = lookupPainting(qr_payload);
    drawResultScreen(name ? name : qr_payload, name != nullptr);

    unsigned long result_until = now + RESULT_DURATION_MS;

    // Wait out the timeout — camera is already stopped from step 2
    while (millis() < result_until) {
      // Keep draining any stray camera frames that may come in
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb) esp_camera_fb_return(fb);
      delay(10);
    }

    // Step 5: Done — empty everything
    qr_detected = false;
    memset(qr_payload, 0, 64);
    tft.fillScreen(TFT_BLACK);

    // Drain any buffered frames before resuming
    drainCameraBuffers();

    // Start from zero — back to live video
    return;
  }

  // ---- LIVE VIDEO CONTINUOUSLY ----
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  // Display the live frame
  tft.pushImage(0, 0, fb->width, fb->height, (uint16_t *)fb->buf);

  // ---- QR DETECTION ON THIS FRAME ----
  // Convert to grayscale
  convertToGrayscale(fb);

  // Return camera frame BEFORE stopping the camera
  // (step 2: video has already been "paused" by the result state above)
  esp_camera_fb_return(fb);

  // Step 3: Scan this frame with quirc
  // Core 0 does the heavy quirc decode while camera is free
  // We signal Core 0 by filling grayscale_buf (it watches for new data)

  // Actually, let's do the scan right here in Core 1 since Core 0 adds complexity
  // and we want to guarantee the camera is fully stopped before scanning starts
  // Run quirc decode in-place on grayscale_buf
  int w = 0, h = 0;
  uint8_t *qbuf = quirc_begin(qr, &w, &h);
  if (qbuf) {
    memcpy(qbuf, grayscale_buf, 320 * 240);
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

      strncpy(qr_payload, tmp, 63);
      qr_payload[63] = '\0';
      qr_detected = true;
    }
  }

  // Continue live video on next loop iteration (unless QR was found → result state takes over)
}

// ------------------- CORE 0: QR SCANNER (unused in this version) -------------------
void scannerTask(void *pvParameters) {
  // This task is not used in this version — scanning happens in Core 1 loop
  // after the camera frame is returned, to guarantee camera is stopped first.
  for (;;) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
