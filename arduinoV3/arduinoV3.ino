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

// Internal DRAM — no PSRAM cache thrashing
static uint8_t scan_buf[320 * 240];

struct quirc *qr = nullptr;

volatile bool qr_detected = false;
volatile bool frame_ready_for_scan = false;
char qr_payload[64] = "";
String last_qr_payload = "";
unsigned long last_qr_time = 0;
const unsigned long QR_COOLDOWN_MS = 3000;
const unsigned long RESULT_DURATION_MS = 4000;

bool showing_result = false;
unsigned long result_until = 0;
const int FRAMES_BETWEEN_SCANS = 25;
int frame_counter = 0;

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
  tft.setSPISpeed(20000000); // 20MHz instead of default ~40MHz

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

  xTaskCreatePinnedToCore(scannerTask, "Scanner", 10000, NULL, 1, &Task0, 0);
  Serial.println("Ready");
}

const char *lookupPainting(const char *payload) {
  if (strcmp(payload, "MonaLisa") == 0)    return "Mona Lisa";
  if (strcmp(payload, "StarryNight") == 0)  return "Starry Night";
  if (strcmp(payload, "TheScream") == 0)    return "The Scream";
  return nullptr;
}

void drawResultOverlay(const char *painting_name, bool known) {
  tft.fillRect(0, 194, 320, 46, TFT_DARKGREY);
  tft.fillRect(0, 197, 320, 40, TFT_BLUE);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.drawString(painting_name, 5, 200, 2);
  tft.setTextColor(known ? TFT_GREEN : TFT_YELLOW, TFT_BLUE);
  tft.drawString(known ? "Painting Identified" : "Unknown QR", 5, 218, 1);
  unsigned long remaining = (result_until > millis()) ? (result_until - millis()) : 0;
  uint16_t barW = map(remaining, 0, RESULT_DURATION_MS, 0, 316);
  tft.drawRect(2, 234, 316, 3, TFT_WHITE);
  tft.fillRect(2, 234, barW, 3, TFT_GREEN);
}

// ============================================================
// CORE 1 — Display loop
// ============================================================
void loop() {
  unsigned long now = millis();

  if (showing_result && now >= result_until) {
    showing_result = false;
    last_qr_payload = "";
    tft.fillRect(0, 194, 320, 46, TFT_BLACK);
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  // Display frame
  tft.pushImage(0, 0, fb->width, fb->height, (uint16_t *)fb->buf);

  // Feed Core 0's scanner every N frames
  frame_counter++;
  if (frame_counter >= FRAMES_BETWEEN_SCANS && !frame_ready_for_scan) {
    frame_counter = 0;
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
    frame_ready_for_scan = true;
  }

  esp_camera_fb_return(fb);

  if (qr_detected) {
    qr_detected = false;
    if (strcmp(qr_payload, last_qr_payload.c_str()) != 0 ||
        (now - last_qr_time) >= QR_COOLDOWN_MS) {
      last_qr_payload = String(qr_payload);
      last_qr_time = now;
      const char *name = lookupPainting(qr_payload);
      drawResultOverlay(name ? name : qr_payload, name != nullptr);
      showing_result = true;
      result_until = now + RESULT_DURATION_MS;
    }
  }

  if (showing_result) {
    const char *name = lookupPainting(last_qr_payload.c_str());
    drawResultOverlay(name ? name : last_qr_payload.c_str(), name != nullptr);
  }
}

// ============================================================
// CORE 0 — QR scanner
// ============================================================
void scannerTask(void *pvParameters) {
  for (;;) {
    if (frame_ready_for_scan) {
      frame_ready_for_scan = false;

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
          strncpy(qr_payload, tmp, 63);
          qr_payload[63] = '\0';
          qr_detected = true;
          Serial.print("QR: ");
          Serial.println(qr_payload);
        }
      }
    } else {
      vTaskDelay(5 / portTICK_PERIOD_MS);
    }
  }
}
