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

volatile bool qr_found = false;
char qr_payload[64] = "";

volatile bool frame_ready_for_scan = false;
volatile bool allow_scan = true;

const unsigned long RESULT_DURATION_MS = 5000;

const int FRAMES_BETWEEN_SCANS = 30;
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

const char *getPaintingPrompt(const char *payload) {
  if (strcmp(payload, "MonaLisa") == 0)
    return "Mona Lisa - Da Vinci";
  if (strcmp(payload, "StarryNight") == 0)
    return "Starry Night - Van Gogh";
  if (strcmp(payload, "TheScream") == 0)
    return "The Scream - Munch";
  return nullptr;
}

void showResult(const char *prompt) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(prompt, 160, 110, 4);
}

void drainCameraBuffers() {
  for (int i = 0; i < 5; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
  }
}

// ------------------- CORE 1: VIDEO LOOP -------------------
void loop() {
  if (qr_found) {
    qr_found = false;

    // 1. Wake up display and clear any pending SPI operations
    tft.writecommand(TFT_DISPLAYON);
    tft.fillScreen(TFT_BLACK);

    // 2. Drain camera to get a clean state
    drainCameraBuffers();

    // 3. Show result
    const char *prompt = getPaintingPrompt(qr_payload);
    if (prompt) {
      showResult(prompt);
    } else {
      showResult(qr_payload);
    }

    // 4. Wait for result duration
    delay(RESULT_DURATION_MS);

    // 5. Turn display OFF before returning to video
    tft.writecommand(TFT_DISPLAYOFF);
    tft.fillScreen(TFT_BLACK);

    // 6. Drain accumulated camera frames
    drainCameraBuffers();
    drainCameraBuffers();
    delay(100);
    drainCameraBuffers();

    // 7. Wake display back up
    tft.writecommand(TFT_DISPLAYON);

    // 8. Reset state
    memset(qr_payload, 0, 64);
    allow_scan = true;
    frame_counter = 0;
    frame_ready_for_scan = false;

    return;
  }

  // ---- LIVE VIDEO ----
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  tft.pushImage(0, 0, fb->width, fb->height, (uint16_t *)fb->buf);

  frame_counter++;
  if (frame_counter >= FRAMES_BETWEEN_SCANS && !frame_ready_for_scan && allow_scan) {
    frame_counter = 0;

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
    frame_ready_for_scan = true;
  }

  esp_camera_fb_return(fb);
}

// ------------------- CORE 0: QR SCANNER -------------------
void scannerTask(void *pvParameters) {
  for (;;) {
    if (!frame_ready_for_scan) {
      vTaskDelay(5 / portTICK_PERIOD_MS);
      continue;
    }

    if (!allow_scan) {
      frame_ready_for_scan = false;
      vTaskDelay(5 / portTICK_PERIOD_MS);
      continue;
    }

    frame_ready_for_scan = false;

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
        strncpy(qr_payload, tmp, 63);
        qr_payload[63] = '\0';
        qr_found = true;
        allow_scan = false;
        Serial.print("QR: ");
        Serial.println(qr_payload);
      }
    }
  }
}
