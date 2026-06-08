#include "esp_camera.h"
#include <Adafruit_GFX.h>    // Librería gráfica base de Adafruit
#include <Adafruit_ST7789.h> // Librería específica para el chip ST7789
#include <SPI.h>
#include "soc/soc.h"           // Necesario para desactivar la protección Brownout
#include "soc/rtc_cntl_reg.h"  // Necesario para desactivar la protección Brownout

// Definición de pines para la pantalla TFT ST7789 con el pin CS incluido
#define TFT_CS     13  // Pin de selección de chip (Chip Select)
#define TFT_RST    12  // Pin de reinicio de la pantalla (Reset)
#define TFT_DC      2  // Pin de selección de Comando/Datos (Data/Command)

// Configuración de pines de la ESP32-CAM (Modelo AI-Thinker)
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

// Inicializamos el objeto de la pantalla incluyendo los tres pines de control (CS, DC, RST)
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

void setup() {
  // Desactivamos el detector de caídas de tensión para evitar que la placa se reinicie sola
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  
  Serial.begin(115200);
  
  // Inicializamos la pantalla ST7789 con su tamaño físico de 240x240 píxeles
  tft.init(240, 240);
  tft.setRotation(0);          // Cambia este número (0-3) si la imagen te sale al revés
  tft.fillScreen(ST77XX_BLACK); // Limpiamos la pantalla pintándola de negro usando el término correcto

  // Configuración de los parámetros del módulo de la cámara
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
  
  // Ajustamos la velocidad del reloj de la cámara a 24MHz para mayor fluidez
  config.xclk_freq_hz = 24000000; 
  
  // Formato RGB565: envía los colores listos en binario sin pasar por el formato JPEG
  config.pixel_format = PIXFORMAT_RGB565; 

  // Ajustamos el tamaño del cuadro exactamente a la resolución de tu pantalla (240x240)
  config.frame_size = FRAMESIZE_240X240; 
  config.jpeg_quality = 12; 
  config.fb_count = 1;      

  // Inicializamos el hardware de la cámara con la configuración establecida
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Error crítico al iniciar la cámara: 0x%x", err);
    return;
  }
}

void loop() {
  // Solicitamos a la cámara que capture un cuadro de video en la memoria buffer
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Error: No se pudo capturar la imagen");
    return;
  }

  // Dibujamos el mapa de píxeles capturado directamente en la pantalla TFT (coordenadas 0,0)
  tft.drawRGBBitmap(0, 0, (uint16_t*)fb->buf, 240, 240);

  // Liberamos la memoria del cuadro actual para dejar espacio al siguiente cuadro en el próximo bucle
  esp_camera_fb_return(fb);
}
