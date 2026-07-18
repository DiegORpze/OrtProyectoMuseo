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

// ---- State machine ----
//  LIVE_STREAMING  : show video, periodically run QR scan
//  SHOWING_RESULT  : freeze on last frame, show QR text, return to streaming after timeout
enum DisplayState { LIVE_STREAMING, SHOWING_RESULT };
DisplayState state = LIVE_STREAMING;
unsigned long result_until = 0;
const unsigned long RESULT_DURATION_MS = 4000;

// QR scan runs every N frames (~every ~500ms at 30fps)
const int FRAMES_BETWEEN_SCANS = 15;
int frame_counter = 0;

// Debounce: don't re-trigger on the same QR within this many ms
String last_qr_payload = "";
unsigned long last_qr_time = 0;
const unsigned long QR_COOLDOWN_MS = 3000;

// Last captured frame (used to freeze video during result display)
camera_fb_t * frozen_fb = nullptr;

void setup() {
  Serial.begin(230400);
  delay(1000);

  // Manual camera reset via GPIO 12
  pinMode(12, OUTPUT);
  digitalWrite(12, LOW);
  delay(50);
  digitalWrite(12, HIGH);
  delay(150);

  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(false);

  // Grayscale buffer for QR scanning
  grayscale_buf = (uint8_t *)ps_malloc(320 * 240);
  if (!grayscale_buf) {
    Serial.println("Failed to allocate grayscale buffer!");
  }

  // QR decoder
  qr = quirc_new();
  if (!qr) {
    Serial.println("Failed to allocate quirc!");
  } else {
    quirc_resize(qr, 320, 240);
  }

  // Camera init
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
  config.fb_count = 2;               // Camera double-buffering
  config.grab_mode = CAMERA_GRAB_LATEST;

  Serial.println("Initializing camera...");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    tft.fillScreen(TFT_RED);
    tft.drawString("Camera Error", 10, 10, 2);
    Serial.printf("Error: 0x%x\n", err);
    return;
  }

  Serial.println("Ready");
}

const char *lookupPainting(const char *payload) {
  if (strcmp(payload, "MonaLisa") == 0)    return "Mona Lisa";
  if (strcmp(payload, "StarryNight") == 0)  return "Starry Night";
  if (strcmp(payload, "TheScream") == 0)    return "The Scream";
  return nullptr; // unknown
}

void drawResultScreen(const char *painting_name, bool known) {
  tft.fillScreen(TFT_BLACK);

  tft.setTextDatum(MC_DATUM);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(painting_name, 160, 85, 4);

  tft.setTextColor(known ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
  tft.drawString(known ? "Painting Identified" : "Unknown QR Code", 160, 130, 2);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Returning to live video...", 160, 190, 2);

  tft.setTextDatum(TL_DATUM);
}

bool tryScanQR(camera_fb_t *fb) {
  if (!qr || !grayscale_buf) return false;

  // Build grayscale from RGB565
  uint8_t *src = fb->buf;
  for (int i = 0; i < 320 * 240; i++) {
    uint16_t pixel = (src[i * 2] << 8) | src[i * 2 + 1];
    uint8_t r5 = (pixel >> 11) & 0x1F;
    uint8_t g6 = (pixel >> 5)  & 0x3F;
    uint8_t b5 =  pixel        & 0x1F;
    uint8_t r = (r5 << 3) | (r5 >> 2);
    uint8_t g = (g6 << 2) | (g6 >> 4);
    uint8_t b = (b5 << 3) | (b5 >> 2);
    grayscale_buf[i] = (r * 77 + g * 150 + b * 29) >> 8;
  }

  // Run QR detection
  int w = 0, h = 0;
  uint8_t *qbuf = quirc_begin(qr, &w, &h);
  memcpy(qbuf, grayscale_buf, w * h);
  quirc_end(qr);

  if (quirc_count(qr) == 0) return false;

  struct quirc_code code;
  struct quirc_data data;
  quirc_extract(qr, 0, &code);
  if (quirc_decode(&code, &data) != QUIRC_SUCCESS) return false;

  // Null-terminate and clean payload
  int len = data.payload_len;
  if (len > 63) len = 63;
  char tmp[64];
  memcpy(tmp, data.payload, len);
  tmp[len] = '\0';

  // Strip trailing whitespace
  for (int i = 0; i < len; i++) {
    if (tmp[i] == ' ' || tmp[i] == '\n' || tmp[i] == '\r' || tmp[i] == '\t') {
      tmp[i] = '\0';
      break;
    }
  }

  Serial.print("QR: ");
  Serial.println(tmp);

  // Cooldown check
  unsigned long now = millis();
  if (strcmp(tmp, last_qr_payload.c_str()) == 0 &&
      (now - last_qr_time) < QR_COOLDOWN_MS) {
    return false; // same QR, still cooling down
  }

  last_qr_payload = String(tmp);
  last_qr_time = now;

  // Look up painting name
  const char *name = lookupPainting(tmp);
  if (name) {
    drawResultScreen(name, true);
  } else {
    drawResultScreen(tmp, false);
  }

  return true;
}

// ---------------------------------------------------------------------------
void loop() {
  unsigned long now = millis();

  // ---- RESULT DISPLAY MODE ----
  // Show result for a fixed duration, keeping camera buffers healthy.
  // We freeze on the last video frame (no new pushImage calls) and draw
  // the QR text on top via drawResultScreen which was called when we entered
  // this state. We only drain frames from the camera so buffers stay fresh.
  if (state == SHOWING_RESULT) {
    // Drain any buffered camera frames to keep DMA healthy
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);

    if (now >= result_until) {
      state = LIVE_STREAMING;
      tft.fillScreen(TFT_BLACK);   // clean slate before resuming video
    }
    return;                         // <<-- do NOT display, just drain
  }

  // ---- LIVE STREAMING ----
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  // Always push video frame to display immediately
  tft.pushImage(0, 0, fb->width, fb->height, (uint16_t *)fb->buf);

  frame_counter++;

  // Periodic QR scan: don't scan every frame — run every N frames instead
  // This gives the camera buffer pool time to breathe and keeps video smooth.
  if (frame_counter >= FRAMES_BETWEEN_SCANS) {
    frame_counter = 0;

    if (tryScanQR(fb)) {
      // QR was found and result screen was drawn.
      // Release the camera frame BEFORE entering SHOWING_RESULT so the
      // camera DMA never fights with the display SPI bus.
      esp_camera_fb_return(fb);

      state = SHOWING_RESULT;
      result_until = now + RESULT_DURATION_MS;
      return;   // enter result display mode
    }
  }

  // Normal: return frame to camera for reuse
  esp_camera_fb_return(fb);
}
