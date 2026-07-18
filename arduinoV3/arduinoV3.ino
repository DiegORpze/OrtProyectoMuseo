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

// Shared variables
volatile bool frame_ready_for_scan = false;
uint8_t *grayscale_buf = nullptr;

// QR result passed from Core 0 -> Core 1
volatile bool new_qr_data = false;
char new_payload[64] = "";
String last_scanned_payload = "";

// State machine
enum DisplayState { LIVE_VIDEO, SHOWING_RESULT };
volatile DisplayState state = LIVE_VIDEO;
unsigned long result_display_until = 0;
const unsigned long RESULT_DISPLAY_MS = 4000;

// Task handle for Core 0
TaskHandle_t Task0;

void setup() {
  Serial.begin(230400);
  delay(1000);

  // Manual reset
  pinMode(12, OUTPUT);
  digitalWrite(12, LOW);
  delay(50);
  digitalWrite(12, HIGH);
  delay(150);

  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(false);

  // Allocate grayscale buffer for QR scanning
  if (ESP.getPsramSize() > 0) {
    grayscale_buf = (uint8_t *)ps_malloc(320 * 240);
  } else {
    grayscale_buf = (uint8_t *)malloc(320 * 240);
  }

  if (!grayscale_buf) {
    Serial.println("Failed to allocate grayscale buffer!");
  }

  // Initialize QR Decoder
  qr = quirc_new();
  if (!qr) {
    Serial.println("Failed to allocate quirc memory");
  } else {
    if (quirc_resize(qr, 320, 240) < 0) {
      Serial.println("Failed to resize quirc");
    }
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

  // Start Core 0 task
  xTaskCreatePinnedToCore(
    scannerTask,
    "Scanner",
    10000,
    NULL,
    1,
    &Task0,
    0
  );
}

// Map QR payload to painting display name
String getPaintingName(const char *payload) {
  if (strcmp(payload, "MonaLisa") == 0)    return "Mona Lisa";
  if (strcmp(payload, "StarryNight") == 0)  return "Starry Night";
  if (strcmp(payload, "TheScream") == 0)    return "The Scream";
  return String(payload); // unknown — show raw text
}

// Draw the result overlay (always starts from a clean black screen)
void drawResultScreen(const String &name, bool known) {
  tft.fillScreen(TFT_BLACK);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(name, 160, 90, 4);

  if (known) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Painting Identified", 160, 135, 2);
  } else {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Unknown QR Code", 160, 135, 2);
  }

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Returning to live video...", 160, 195, 2);

  tft.setTextDatum(TL_DATUM); // reset
}

// ------------------- CORE 1: VIDEO & RESULT LOOP -------------------
void loop() {
  unsigned long now = millis();

  switch (state) {

    // ---- LIVE VIDEO ----
    case LIVE_VIDEO: {
      camera_fb_t * fb = esp_camera_fb_get();
      if (!fb) return;

      // Draw video frame to screen
      tft.pushImage(0, 0, fb->width, fb->height, (uint16_t *)fb->buf);

      // Feed grayscale to QR scanner when idle
      if (!frame_ready_for_scan && grayscale_buf) {
        uint8_t *src = fb->buf;
        for (int i = 0; i < 320 * 240; i++) {
          uint16_t pixel = (src[i*2] << 8) | src[i*2+1];
          uint8_t r5 = (pixel >> 11) & 0x1F;
          uint8_t g6 = (pixel >> 5)  & 0x3F;
          uint8_t b5 = pixel & 0x1F;
          uint8_t r = (r5 << 3) | (r5 >> 2);
          uint8_t g = (g6 << 2) | (g6 >> 4);
          uint8_t b = (b5 << 3) | (b5 >> 2);
          grayscale_buf[i] = (r * 77 + g * 150 + b * 29) >> 8;
        }
        frame_ready_for_scan = true;
      }

      // QR found — transition to result display
      if (new_qr_data) {
        new_qr_data = false;

        Serial.print("QR scanned: ");
        Serial.println(new_payload);

        String name = getPaintingName(new_payload);
        bool known = (name != String(new_payload));

        // CRITICAL: return the camera frame BEFORE touching the screen.
        // This prevents camera DMA and display SPI from fighting.
        esp_camera_fb_return(fb);

        // Now switch state and draw — camera buffer is already returned
        state = SHOWING_RESULT;
        result_display_until = now + RESULT_DISPLAY_MS;

        drawResultScreen(name, known);
        break;
      }

      esp_camera_fb_return(fb);
      break;
    }

    // ---- SHOWING_RESULT ----
    case SHOWING_RESULT: {
      // Drain any frames the camera buffered while we were in LIVE_VIDEO.
      // This keeps the camera's buffer pool healthy so the next frame
      // we grab is guaranteed fresh, not a stale one from 4 seconds ago.
      camera_fb_t * fb = esp_camera_fb_get();
      if (fb) esp_camera_fb_return(fb);

      if (now >= result_display_until) {
        state = LIVE_VIDEO;
        tft.fillScreen(TFT_BLACK); // clean slate before resuming video
      }
      break;
    }
  }
}

// ------------------- CORE 0: QR SCANNER LOOP -------------------
void scannerTask(void *pvParameters) {
  for (;;) {
    if (frame_ready_for_scan) {

      int w = 0, h = 0;
      uint8_t *qbuf = quirc_begin(qr, &w, &h);

      if (qbuf) {
        memcpy(qbuf, grayscale_buf, w * h);
      }
      quirc_end(qr);

      int count = quirc_count(qr);
      if (count > 0) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(qr, 0, &code);

        if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
          int len = data.payload_len;
          if (len > 63) len = 63;

          char temp_buf[64];
          memcpy(temp_buf, data.payload, len);
          temp_buf[len] = '\0';

          // Strip trailing whitespace
          for (int i = 0; i < len; i++) {
            if (temp_buf[i] == ' ' || temp_buf[i] == '\n' ||
                temp_buf[i] == '\r' || temp_buf[i] == '\t') {
              temp_buf[i] = '\0';
              break;
            }
          }

          strncpy(new_payload, temp_buf, 63);
          new_payload[63] = '\0';
          new_qr_data = true;
        }
      }

      frame_ready_for_scan = false;
    } else {
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
  }
}
