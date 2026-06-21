#include "esp_camera.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Pines de control de la pantalla
#define TFT_CS   13
#define TFT_RST  12
#define TFT_DC    2

// Pines SPI personalizados
#define TFT_MOSI 15
#define TFT_SCLK 14

// Creamos un bus SPI independiente
SPIClass hspi(HSPI);

// Pantalla usando CS, DC y RST
Adafruit_ST7789 tft = Adafruit_ST7789(&hspi, TFT_CS, TFT_DC, TFT_RST);

// Configuración AI Thinker
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

void setup() {

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);

  // Inicializar SPI personalizado
  hspi.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  // Inicializar pantalla
  tft.init(240, 240);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  // Configuración cámara
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
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 24000000;

  config.pixel_format = PIXFORMAT_RGB565;

  config.frame_size = FRAMESIZE_240X240;

  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Error cámara: 0x%x\n", err);

    while (true) {
      tft.fillScreen(ST77XX_RED);
      delay(500);
      tft.fillScreen(ST77XX_BLACK);
      delay(500);
    }
  }

  tft.fillScreen(ST77XX_GREEN);
  delay(1000);
  tft.fillScreen(ST77XX_BLACK);
}

void loop() {

  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb) {
    return;
  }

  tft.drawRGBBitmap(
      0,
      0,
      (uint16_t*)fb->buf,
      240,
      240);

  esp_camera_fb_return(fb);
}
