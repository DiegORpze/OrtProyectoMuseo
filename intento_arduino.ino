#include "esp_camera.h"        // Librería para controlar la cámara ESP32-CAM
#include "soc/soc.h"           // Acceso a registros del sistema
#include "soc/rtc_cntl_reg.h"  // Control del detector de brownout (caída de tensión)

// ===============================
// Definición de pines ESP32-CAM
// Modelo: AI-Thinker
// ===============================
#define PWDN_GPIO_NUM     32   // Pin de apagado de la cámara
#define RESET_GPIO_NUM    -1   // Sin pin de reset externo
#define XCLK_GPIO_NUM      0   // Reloj externo de la cámara
#define SIOD_GPIO_NUM     26   // Datos I2C de configuración
#define SIOC_GPIO_NUM     27   // Reloj I2C de configuración
#define Y9_GPIO_NUM       35   // Datos de imagen D7
#define Y8_GPIO_NUM       34   // Datos de imagen D6
#define Y7_GPIO_NUM       39   // Datos de imagen D5
#define Y6_GPIO_NUM       36   // Datos de imagen D4
#define Y5_GPIO_NUM       21   // Datos de imagen D3
#define Y4_GPIO_NUM       19   // Datos de imagen D2
#define Y3_GPIO_NUM       18   // Datos de imagen D1
#define Y2_GPIO_NUM        5   // Datos de imagen D0
#define VSYNC_GPIO_NUM    25   // Sincronización vertical
#define HREF_GPIO_NUM     23   // Referencia horizontal
#define PCLK_GPIO_NUM     22   // Reloj de píxel

void setup() {

  // Desactiva el detector de brownout.
  // Esto evita reinicios inesperados cuando el consumo de corriente es alto.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Inicializa la comunicación serie a 1 Mbps.
  // Se usa una velocidad alta para transmitir imágenes rápidamente.
  Serial.begin(1000000);

  // Estructura de configuración de la cámara
  camera_config_t config;

  // Configuración del temporizador PWM para generar el reloj de la cámara
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  // Asignación de pines de datos
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  // Asignación de pines de control
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;

  // Bus SCCB (similar a I2C) para configurar el sensor
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;

  // Pines de alimentación y reset
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  // Frecuencia del reloj principal de la cámara (24 MHz)
  config.xclk_freq_hz = 24000000;

  // Formato de imagen JPEG para reducir tamaño de transmisión
  config.pixel_format = PIXFORMAT_JPEG;

  // Resolución de captura.
  // QVGA = 320x240 píxeles.
  // Menor resolución = mayor velocidad de transmisión.
  config.frame_size = FRAMESIZE_QVGA;

  // Calidad JPEG:
  // Valores entre 10 y 63.
  // Menor número = mejor calidad y archivo más grande.
  config.jpeg_quality = 16;

  // Número de buffers de imagen.
  // Dos buffers permiten capturar mientras se transmite el frame anterior.
  config.fb_count = 2;

  // Inicializa la cámara con la configuración indicada
  esp_err_t err = esp_camera_init(&config);

  // Si ocurre un error, detener la ejecución
  if (err != ESP_OK) {
    while (true);
  }
}

void loop() {

  // Captura una imagen y obtiene un puntero al frame buffer
  camera_fb_t *fb = esp_camera_fb_get();

  // Si la captura falla, salir de la iteración
  if (!fb) return;

  // ==========================================
  // Protocolo de transmisión por puerto serie
  // ==========================================

  // Envía dos bytes de sincronización.
  // El receptor puede buscarlos para detectar
  // el inicio de una nueva imagen.
  Serial.write(0xAA);
  Serial.write(0xBB);

  // Obtiene el tamaño de la imagen JPEG capturada
  uint32_t size = fb->len;

  // Envía el tamaño en 4 bytes
  // Esto permite al receptor saber cuántos bytes leer.
  Serial.write((uint8_t*)&size, 4);

  // Envía todos los datos JPEG de la imagen
  Serial.write(fb->buf, fb->len);

  // Libera el buffer para que pueda reutilizarse
  esp_camera_fb_return(fb);

  // Pequeña pausa para evitar saturar el puerto serie
  delay(30);
}
