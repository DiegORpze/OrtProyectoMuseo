#include <quirc.h>
#include <quirc_internal.h>

#include <quirc.h>
#include <quirc_internal.h>

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
int frame_counter = 0;

void setup() {
  Serial.begin(115200);
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
  tft.setSwapBytes(true); 

  // Initialize QR Decoder
  qr = quirc_new();
  if (!qr) {
    Serial.println("Failed to allocate quirc memory");
  } else {
    // QVGA is 320x240
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
  config.xclk_freq_hz = 16000000; 
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
}

void loop() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) return;

  // 1. Draw the live video to the screen first (keeps VR feeling smooth)
  tft.pushImage(0, 0, fb->width, fb->height, (uint16_t *)fb->buf);

  // 2. Only scan for QR codes every 15 frames to prevent video stutter
  frame_counter++;
  if (frame_counter >= 15) {
    frame_counter = 0;
    scanQRCode(fb);
  }

  esp_camera_fb_return(fb);
}

void scanQRCode(camera_fb_t *fb) {
  if (!qr) return;

  uint8_t *grayscale_buf = (uint8_t *)malloc(320 * 240);
  if (!grayscale_buf) return;

  // RGB565 to Grayscale conversion
  for (int i = 0; i < 320 * 240; i++) {
    uint16_t pixel = (fb->buf[i*2] << 8) | fb->buf[i*2+1]; 
    uint8_t r = (pixel >> 11) & 0x1F;
    uint8_t g = (pixel >> 5) & 0x3F;
    uint8_t b = pixel & 0x1F;
    grayscale_buf[i] = (r * 77 + g * 150 + b * 29) >> 3; 
  }

  // Feed image to quirc
  int w = 0, h = 0;
  // quirc_begin returns the pointer to the internal buffer we need to write to
  uint8_t *qbuf = quirc_begin(qr, &w, &h);
  
  if (qbuf) {
    // Copy our grayscale data into quirc's buffer
    memcpy(qbuf, grayscale_buf, w * h);
  }
  // Tell quirc we are done writing the image and it can start decoding
  quirc_end(qr);

  // Check for QR codes
  int count = quirc_count(qr);
  if (count > 0) {
    struct quirc_code code;
    struct quirc_data data;
    quirc_extract(qr, 0, &code);
    
    if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
      String payload = String((const char *)data.payload);
      Serial.print("QR Code Detected: ");
      Serial.println(payload);

      int paintingID = payload.toInt();

      // --- MUSEUM LOGIC HERE ---
      switch (paintingID) {
        case 1:
          tft.fillRect(0, 210, 240, 30, TFT_BLUE);
          tft.setTextColor(TFT_WHITE, TFT_BLUE);
          tft.drawString("Painting 1: Mona Lisa", 5, 215, 2);
          break;
          
        case 2:
          tft.fillRect(0, 210, 240, 30, TFT_RED);
          tft.setTextColor(TFT_WHITE, TFT_RED);
          tft.drawString("Painting 2: Starry Night", 5, 215, 2);
          break;
          
        case 3:
          tft.fillRect(0, 210, 240, 30, TFT_GREEN);
          tft.setTextColor(TFT_BLACK, TFT_GREEN);
          tft.drawString("Painting 3: The Scream", 5, 215, 2);
          break;
          
        default:
          tft.fillRect(0, 210, 240, 30, TFT_BLACK);
          tft.setTextColor(TFT_RED, TFT_BLACK);
          tft.drawString("Unknown Painting", 5, 215, 2);
          break;
      }
    }
  }

  free(grayscale_buf);
}