#include "esp_camera.h"
#include <SPI.h>
#include <TFT_eSPI.h>

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
  tft.setRotation(1); 
  tft.fillScreen(TFT_BLACK);

  // --- SCREEN DRIVER TEST ---
  // If these colors look wrong, your User_Setup.h driver is wrong.
  tft.fillRect(0, 0, 80, 80, TFT_RED);
  tft.fillRect(80, 0, 80, 80, TFT_GREEN);
  tft.fillRect(160, 0, 80, 80, TFT_BLUE);
  tft.fillRect(240, 0, 80, 80, TFT_WHITE);
  tft.setTextColor(TFT_BLACK);
  tft.drawString("R", 35, 30, 4);
  tft.drawString("G", 115, 30, 4);
  tft.drawString("B", 195, 30, 4);
  tft.drawString("W", 275, 30, 4);
  
  delay(3000); // Wait 3 seconds so you can verify the colors

  tft.fillScreen(TFT_BLACK);
  
  // Turned OFF to test if this fixes the color waves
  tft.setSwapBytes(false); 

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
  config.fb_count = 1;

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

  tft.pushImage(0, 0, fb->width, fb->height, (uint16_t *)fb->buf);
  esp_camera_fb_return(fb);
}