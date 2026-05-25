#include "esp_camera.h"
#include "soc/soc.h"           // Ajouter en haut du code si pas présent
#include "soc/rtc_cntl_reg.h"  // Ajouter en haut du code si pas présent

// Pines de la ESP32-CAM (Modelo AI-Thinker)
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
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // <- AJOUTER CETTE LIGNE EN PREMIER
  // Velocidad muy alta para permitir transmisión de video fluida
  Serial.begin(1000000); 
  
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
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 24000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Resolución recomendada para fluidez por cable: SVGA (800x600) o VGA (640x480)
  config.frame_size = FRAMESIZE_QVGA; 
  config.jpeg_quality = 16; // Calidad (10-63, menor número es mejor calidad)
  config.fb_count = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    while (true); // Detener si hay error
  }
}

void loop() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) return;

  // Enviar un encabezado especial para sincronizar el inicio del frame
  Serial.write(0xAA);
  Serial.write(0xBB);
  
  // Enviar el tamaño de la imagen (4 bytes)
  uint32_t size = fb->len;
  Serial.write((uint8_t*)&size, 4);

  // Enviar los bytes reales de la imagen JPEG
  Serial.write(fb->buf, fb->len);

  esp_camera_fb_return(fb);
  
  // Pequeña pausa para no saturar el buffer serie
  delay(30); 
}
