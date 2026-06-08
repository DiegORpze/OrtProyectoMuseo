#include "esp_camera.h"
#include <Adafruit_GFX.h>    // Librería gráfica base de Adafruit
#include <Adafruit_ST7789.h> // Librería específica para el chip ST7789
#include <TJpg_Decoder.h>    // Librería para decodificar imágenes JPEG en la pantalla
#include <SPI.h>
#include "soc/soc.h"           // Necesario para desactivar el Brownout
#include "soc/rtc_cntl_reg.h"  // Necesario para desactivar el Brownout

// Definición de pines para la pantalla TFT ST7789
#define TFT_CS     13
#define TFT_RST    12
#define TFT_DC      2

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

// Inicializamos el objeto de la pantalla ST7789 indicando sus pines de control
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// Esta función la usa automáticamente la librería TJpg_Decoder para ir dibujando bloques de píxeles en la pantalla
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  // Si el bloque de la imagen se sale de los límites de la pantalla, detenemos el dibujo
  if ( y >= tft.height() ) return false;
  
  // Dibuja el bloque de píxeles RGB en las coordenadas correspondientes
  tft.drawRGBBitmap(x, y, bitmap, w, h);
  return true;
}

void setup() {
  // Desactivamos el detector de caídas de tensión (Brownout) para evitar reinicios por consumo de la cámara
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  
  Serial.begin(115200);
  
  // Inicializamos la pantalla ST7789 con su resolución nativa de 240x240 píxeles
  tft.init(240, 240);
  tft.setRotation(0);          // Ajusta el número (0-3) para rotar la imagen si te sale de cabeza
  tft.fillScreen(ST77XX_BLACK); // Limpiamos la pantalla pintándola de negro

  // Configuramos el decodificador de imágenes JPEG
  TJpg_Decoder.setJpgScale(1); // Escala 1 = Tamaño real de la foto capturada
  TJpg_Decoder.setCallback(tft_output); // Le asignamos la función que dibuja en la pantalla

  // Configuración de los parámetros de la cámara
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
  
  // Elevamos la frecuencia a 24MHz para acelerar el procesamiento de la cámara
  config.xclk_freq_hz = 24000000; 
  config.pixel_format = PIXFORMAT_JPEG; // Obligatorio guardar en JPEG para que funcione la librería de descompresión

  // Ajustamos la resolución exactamente a 240x240 para que encaje perfecto en tu ST7789
  config.frame_size = FRAMESIZE_240X240; 
  config.jpeg_quality = 12; // Calidad del JPEG (números bajos significan mejor calidad, prueba entre 10 y 15)
  config.fb_count = 2;      // Activamos el doble buffer de memoria para mejorar los FPS y la fluidez

  // Inicializamos el hardware de la cámara
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Error crítico al iniciar la cámara: 0x%x", err);
    return;
  }
}

void loop() {
  // Solicitamos un cuadro (frame) capturado por el lente de la cámara
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Error: No se pudo capturar la imagen");
    return;
  }

  // Enviamos los bytes del JPEG capturado para que se decodifiquen y se dibujen en la pantalla TFT (coordenadas 0,0)
  TJpg_Decoder.drawJpg(0, 0, fb->buf, fb->len);

  // Liberamos la memoria del cuadro capturado para poder recibir el siguiente en el próximo ciclo
  esp_camera_fb_return(fb);
}
