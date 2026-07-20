// ============================================================
// ESP32-CAM Live Video to TFT7789 Display
// Bulk transfer - no pixel conversion - full frame push
// ============================================================

#include "esp_camera.h"
#include <SPI.h>
#include <TFT_eSPI.h>

// ------------------- CAMERA PIN DEFINITIONS -------------------
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

// ------------------- DISPLAY OBJECTS -------------------------
TFT_eSPI tft = TFT_eSPI();

// Frame dimensions
// Camera always captures 320 wide x 240 tall in QVGA mode
// Display is 240 x 240, so we crop 40 pixels from each side of the camera frame
#define CAM_WIDTH   320
#define CAM_HEIGHT  240
#define DISP_SIZE   240   // square display
#define CROP_OFFSET 40    // columns to skip from left (center crop)

// PSRAM frame buffer for bulk transfer
// We allocate a 240x240 buffer in PSRAM - one properly cropped frame
static uint16_t* frameBuffer = nullptr;

// Debug stage tracking
enum DebugStage {
  STAGE_INIT,
  STAGE_PSRAM_ALLOC,
  STAGE_CAMERA_INIT,
  STAGE_TFT_INIT,
  STAGE_STREAMING
};

// FPS tracking
uint32_t frameCount = 0;
uint32_t fpsLastTime = 0;
uint32_t currentFps = 0;

// ============================================================
// DEBUG: Draw debug screen with stage info and optional message
// ============================================================
void showDebugScreen(const char* stage, const char* msg = nullptr) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(5, 5);
  tft.print("== DEBUG ==");

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(5, 40);
  tft.print(stage);

  if (msg) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(5, 80);
    tft.print(msg);
  }

  // Small spinner dots to show it's not frozen
  static uint8_t dotCount = 0;
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(5, 200);
  for (uint8_t i = 0; i < dotCount % 4; i++) tft.print(".");
  dotCount++;
}

// ============================================================
// Show error screen and halt
// ============================================================
void showError(const char* stage, const char* msg) {
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextSize(2);
  tft.setCursor(5, 5);
  tft.print("ERROR:");
  tft.setCursor(5, 40);
  tft.print(stage);
  tft.setCursor(5, 80);
  tft.print(msg);

  // Freeze
  while (1) { delay(1000); }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  // ---- STAGE 0: INIT ----
  // TFT init first so we can show debug
  tft.init();
  tft.setRotation(3);           // Rotation 3 as requested
  tft.setSwapBytes(false);      // No byte swap - direct RGB565 push
  tft.fillScreen(TFT_BLACK);

  showDebugScreen("0_INIT", "Starting up...");
  delay(500);

  // ---- STAGE 1: PSRAM ALLOCATION ----
  showDebugScreen("1_PSRAM", "Allocating buffer...");

  if (ESP.getPsramSize() == 0) {
    showError("1_PSRAM", "NO PSRAM FOUND!");
  }

  uint32_t psramFree = ESP.getFreePsram();
  char buf[30];
  snprintf(buf, sizeof(buf), "PSRAM: %u KB free", psramFree / 1024);
  showDebugScreen("1_PSRAM", buf);
  delay(500);

  // Allocate 240x240x2 = 115,200 bytes in PSRAM for one cropped frame
  frameBuffer = (uint16_t*)ps_malloc(DISP_SIZE * DISP_SIZE * sizeof(uint16_t));
  if (!frameBuffer) {
    showError("1_PSRAM", "Buffer alloc failed!");
  }
  showDebugScreen("1_PSRAM", "Buffer OK!");
  delay(500);

  // ---- STAGE 2: CAMERA INIT ----
  showDebugScreen("2_CAMERA", "Configuring camera...");

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
  config.frame_size = FRAMESIZE_QVGA;   // 320x240
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;

  showDebugScreen("2_CAMERA", "Calling init...");
  delay(200);

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    snprintf(buf, sizeof(buf), "Err: 0x%x", err);
    showError("2_CAMERA", buf);
  }
  showDebugScreen("2_CAMERA", "Camera OK!");
  delay(500);

  // ---- STAGE 3: TFT INIT ----
  showDebugScreen("3_TFT", "TFT ready");
  snprintf(buf, sizeof(buf), "TFT: %dx%d rot3", tft.width(), tft.height());
  showDebugScreen("3_TFT", buf);
  delay(500);

  // ---- STAGE 4: START STREAMING ----
  showDebugScreen("4_STREAM", "Starting stream...");
  delay(1000);

  // Black fill before streaming begins
  tft.fillScreen(TFT_BLACK);

  fpsLastTime = millis();
  frameCount = 0;

  showDebugScreen("4_STREAM", "GO!");
  delay(500);
}

// ============================================================
// Main display loop
// ============================================================
void loop() {
  // Capture frame
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(10, 100);
    tft.print("NO FRAME!");
    delay(100);
    return;
  }

  // Copy frame to PSRAM buffer for bulk transfer
  // fb->buf is already RGB565, frameBuffer is RGB565 - no conversion needed
  // We crop to 240x240 by skipping the first 40 pixels of each row (center crop)
  uint16_t* dst = frameBuffer;
  uint16_t* src = (uint16_t*)fb->buf + CROP_OFFSET;
  for (int row = 0; row < DISP_SIZE; row++) {
    memcpy(dst, src, DISP_SIZE * 2);  // 240 pixels x 2 bytes = 480 bytes per row
    dst += DISP_SIZE;
    src += CAM_WIDTH;
  }

  // Return frame buffer to camera immediately so it can capture next frame
  esp_camera_fb_return(fb);

  // Bulk push 240x240 to TFT - single DMA transfer
  tft.pushImage(0, 0, DISP_SIZE, DISP_SIZE, frameBuffer);

  // FPS counter - update every second
  frameCount++;
  uint32_t now = millis();
  if (now - fpsLastTime >= 1000) {
    currentFps = frameCount;
    frameCount = 0;
    fpsLastTime = now;
  }
}
