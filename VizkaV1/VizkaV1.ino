#include "esp_camera.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <ESP32QRCodeReader.h>
#include <quirc/quirc.h>

/*
  Se incluye la biblioteca para utilizar la versión
  modificada de quirc que ya tienen instalada.
*/

// =====================================================
// PINES DE LA CÁMARA ESP32-CAM AI-THINKER
// =====================================================

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

// =====================================================
// DIMENSIONES
// =====================================================

constexpr int CAM_WIDTH = 320;
constexpr int CAM_HEIGHT = 240;

constexpr int DISP_SIZE = 240;

/*
  La cámara genera una imagen de 320 × 240.
  Se quitan 40 píxeles de cada lado para obtener 240 × 240.
*/
constexpr int CROP_OFFSET = 40;

// =====================================================
// CONFIGURACIÓN DE FUNCIONAMIENTO
// =====================================================

/*
  Se analiza un QR cada 3 fotogramas.

  Aumentar este valor mejora la fluidez del video,
  pero hace más lenta la detección.

  Disminuirlo hace más rápida la detección,
  pero reduce los FPS.
*/
constexpr uint8_t FRAMES_ENTRE_ESCANEOS = 3;

/*
  Tiempo durante el cual aparece “QR DETECTADO”.
*/
constexpr uint32_t DURACION_AVISO_MS = 900;

/*
  Tiempo sin volver a decodificar el QR antes
  de regresar al video.
*/
constexpr uint32_t TIEMPO_SIN_QR_MS = 1500;

/*
  Límite de seguridad para el texto procesado.
*/
constexpr size_t MAX_TEXTO_QR = 1023;

// =====================================================
// OBJETOS Y BUFFERS
// =====================================================

TFT_eSPI tft = TFT_eSPI();

/*
  Buffer de 240 × 240 píxeles para enviar el
  video recortado a la TFT.
*/
static uint16_t* frameBuffer = nullptr;

/*
  Objeto principal de quirc.
*/
static struct quirc* lectorQuirc = nullptr;

/*
  Estas estructuras son relativamente grandes.
  Se reservan en PSRAM para no llenar la pila del ESP32.
*/
static struct quirc_code* codigoQuirc = nullptr;
static struct quirc_data* datosQuirc = nullptr;

// =====================================================
// ESTADOS DE LA PANTALLA
// =====================================================

enum EstadoPantalla {
  ESTADO_VIDEO,
  ESTADO_AVISO_QR,
  ESTADO_TEXTO_QR
};

EstadoPantalla estadoPantalla = ESTADO_VIDEO;

String textoQRActual = "";

uint32_t inicioAvisoQR = 0;
uint32_t ultimaDeteccionQR = 0;

uint8_t contadorFramesQR = 0;

// =====================================================
// DETENER EL PROGRAMA CON MENSAJE DE ERROR
// =====================================================

void detenerConError(const char* mensaje) {
  tft.fillScreen(TFT_RED);

  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextWrap(true, true);

  tft.setTextDatum(MC_DATUM);
  tft.drawString(
    "ERROR",
    tft.width() / 2,
    55,
    4
  );

  tft.setTextDatum(TC_DATUM);
  tft.drawString(
    mensaje,
    tft.width() / 2,
    110,
    2
  );

  while (true) {
    delay(1000);
  }
}

// =====================================================
// LIMPIAR EL TEXTO DEL QR
// =====================================================

String limpiarTextoQR(const struct quirc_data& datos) {
  String texto = "";

  int longitud = datos.payload_len;

  if (longitud <= 0) {
    return texto;
  }

  if ((size_t)longitud > MAX_TEXTO_QR) {
    longitud = (int)MAX_TEXTO_QR;
  }

  bool espacioPendiente = false;

  for (int i = 0; i < longitud; i++) {
    uint8_t caracter = datos.payload[i];

    if (caracter == '\0') {
      break;
    }

    /*
      Los saltos de línea, retornos y tabulaciones
      se convierten en un único espacio.
    */
    if (
      caracter == '\n' ||
      caracter == '\r' ||
      caracter == '\t'
    ) {
      espacioPendiente = true;
      continue;
    }

    // Ignorar otros caracteres de control.
    if (caracter < 32) {
      continue;
    }

    if (
      espacioPendiente &&
      texto.length() > 0
    ) {
      texto += ' ';
    }

    espacioPendiente = false;
    texto += (char)caracter;
  }

  texto.trim();

  return texto;
}

// =====================================================
// MOSTRAR “QR DETECTADO”
// =====================================================

void mostrarAvisoQR() {
  tft.fillScreen(TFT_BLACK);

  tft.setTextWrap(false, false);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  tft.drawString(
    "OBRA DETECTADA",
    tft.width() / 2,
    tft.height() / 2,
    4
  );
}

// =====================================================
// MOSTRAR EL CONTENIDO DEL QR
// =====================================================

void mostrarTextoQR(const String& texto) {
  tft.fillScreen(TFT_BLACK);

  tft.setTextWrap(false, false);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  const int margen = 10;
  const int anchoDisponible =
    tft.width() - (margen * 2);

  /*
    Fuente 4: grande.
    Fuente 2: mediana.
    Fuente 1: pequeña.
  */
  uint8_t fuente = 4;

  if (
    tft.textWidth(texto, fuente) >
    anchoDisponible
  ) {
    fuente = 2;
  }

  if (
    tft.textWidth(texto, fuente) >
    anchoDisponible
  ) {
    fuente = 1;
  }

  /*
    Mostrar centrado si entra en una línea.
  */
  if (
    tft.textWidth(texto, fuente) <=
    anchoDisponible
  ) {
    tft.setTextDatum(MC_DATUM);

    tft.drawString(
      texto,
      tft.width() / 2,
      tft.height() / 2,
      fuente
    );

    return;
  }

  /*
    Para textos muy largos se utiliza ajuste
    automático en varias líneas.
  */
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextWrap(true, true);

  tft.setCursor(
    margen,
    margen,
    1
  );

  tft.print(texto);
}

// =====================================================
// COPIAR Y RECORTAR EL FOTOGRAMA PARA LA TFT
// =====================================================

void copiarVideoRecortado(const camera_fb_t* fb) {
  const uint16_t* origen =
    reinterpret_cast<const uint16_t*>(fb->buf);

  for (int fila = 0; fila < DISP_SIZE; fila++) {
    const uint16_t* filaOrigen =
      origen +
      (fila * CAM_WIDTH) +
      CROP_OFFSET;

    uint16_t* filaDestino =
      frameBuffer +
      (fila * DISP_SIZE);

    memcpy(
      filaDestino,
      filaOrigen,
      DISP_SIZE * sizeof(uint16_t)
    );
  }
}

// =====================================================
// DETECTAR UN QR EN UN FOTOGRAMA RGB565
// =====================================================

bool detectarQR(
  const camera_fb_t* fb,
  String& textoDetectado
) {
  if (
    fb == nullptr ||
    lectorQuirc == nullptr ||
    codigoQuirc == nullptr ||
    datosQuirc == nullptr
  ) {
    return false;
  }

  if (fb->format != PIXFORMAT_RGB565) {
    return false;
  }

  if (
    fb->width != CAM_WIDTH ||
    fb->height != CAM_HEIGHT
  ) {
    return false;
  }

  const size_t bytesEsperados =
    (size_t)CAM_WIDTH *
    CAM_HEIGHT *
    2;

  if (fb->len < bytesEsperados) {
    return false;
  }

  int anchoQuirc = 0;
  int altoQuirc = 0;

  uint8_t* imagenGris = quirc_begin(
    lectorQuirc,
    &anchoQuirc,
    &altoQuirc
  );

  if (imagenGris == nullptr) {
    return false;
  }

  if (
    anchoQuirc != CAM_WIDTH ||
    altoQuirc != CAM_HEIGHT
  ) {
    quirc_end(lectorQuirc);
    return false;
  }

  /*
    Convertir el fotograma RGB565 a escala de grises.

    Se reconstruye cada píxel usando sus dos bytes.
  */
  const uint8_t* origen = fb->buf;

  const size_t cantidadPixeles =
    (size_t)CAM_WIDTH *
    CAM_HEIGHT;

  for (
    size_t i = 0;
    i < cantidadPixeles;
    i++
  ) {
    const uint16_t pixel =
      ((uint16_t)origen[i * 2] << 8) |
      origen[(i * 2) + 1];

    const uint8_t rojo =
      (uint8_t)(
        ((pixel >> 11) & 0x1F) << 3
      );

    const uint8_t verde =
      (uint8_t)(
        ((pixel >> 5) & 0x3F) << 2
      );

    const uint8_t azul =
      (uint8_t)(
        (pixel & 0x1F) << 3
      );

    /*
      Aproximación de luminancia:
      0,299 R + 0,587 G + 0,114 B.
    */
    imagenGris[i] =
      (uint8_t)(
        (
          (uint16_t)rojo * 77 +
          (uint16_t)verde * 150 +
          (uint16_t)azul * 29
        ) >> 8
      );
  }

  quirc_end(lectorQuirc);

  const int cantidadCodigos =
    quirc_count(lectorQuirc);

  for (
    int i = 0;
    i < cantidadCodigos;
    i++
  ) {
    quirc_extract(
      lectorQuirc,
      i,
      codigoQuirc
    );

    const quirc_decode_error_t error =
      quirc_decode(
        codigoQuirc,
        datosQuirc
      );

    if (error == QUIRC_SUCCESS) {
      String texto =
        limpiarTextoQR(*datosQuirc);

      if (texto.length() > 0) {
        textoDetectado = texto;
        return true;
      }
    }
  }

  return false;
}

// =====================================================
// PROCESAR UNA DETECCIÓN VÁLIDA
// =====================================================

void procesarQRValido(const String& texto) {
  /*
    Se actualiza incluso si es el mismo QR.
    Así sabemos que continúa delante de la cámara.
  */
  ultimaDeteccionQR = millis();

  /*
    Si estábamos mostrando video o apareció
    un QR diferente, empezar nuevamente la secuencia.
  */
  if (
    estadoPantalla == ESTADO_VIDEO ||
    texto != textoQRActual
  ) {
    textoQRActual = texto;

    estadoPantalla = ESTADO_AVISO_QR;
    inicioAvisoQR = millis();

    mostrarAvisoQR();
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  delay(1500);

  // ---------------------------------------------------
  // 1. Inicializar TFT
  // ---------------------------------------------------

  tft.init();
  tft.setRotation(3);

  /*
    Se conserva la misma configuración del código
    de video que ya les funcionó.
  */
  tft.setSwapBytes(false);

  tft.fillScreen(TFT_BLACK);

  // ---------------------------------------------------
  // 2. Comprobar PSRAM
  // ---------------------------------------------------

  if (!psramFound()) {
    detenerConError(
      "No se detecto PSRAM"
    );
  }

  // ---------------------------------------------------
  // 3. Reservar buffer de video
  // ---------------------------------------------------

  frameBuffer = (uint16_t*)ps_malloc(
    DISP_SIZE *
    DISP_SIZE *
    sizeof(uint16_t)
  );

  if (frameBuffer == nullptr) {
    detenerConError(
      "Fallo buffer de video"
    );
  }

  // ---------------------------------------------------
  // 4. Inicializar quirc
  // ---------------------------------------------------

  lectorQuirc = quirc_new();

  if (lectorQuirc == nullptr) {
    detenerConError(
      "Fallo al crear lector QR"
    );
  }

  if (
    quirc_resize(
      lectorQuirc,
      CAM_WIDTH,
      CAM_HEIGHT
    ) < 0
  ) {
    detenerConError(
      "Fallo memoria lector QR"
    );
  }

  codigoQuirc =
    (struct quirc_code*)ps_malloc(
      sizeof(struct quirc_code)
    );

  datosQuirc =
    (struct quirc_data*)ps_malloc(
      sizeof(struct quirc_data)
    );

  if (
    codigoQuirc == nullptr ||
    datosQuirc == nullptr
  ) {
    detenerConError(
      "Fallo buffers QR"
    );
  }

  // ---------------------------------------------------
  // 5. Reinicio eléctrico de la cámara
  // ---------------------------------------------------

  pinMode(PWDN_GPIO_NUM, OUTPUT);

  digitalWrite(
    PWDN_GPIO_NUM,
    HIGH
  );

  delay(100);

  digitalWrite(
    PWDN_GPIO_NUM,
    LOW
  );

  delay(100);

  // ---------------------------------------------------
  // 6. Configuración de la cámara
  // ---------------------------------------------------

  camera_config_t config = {};

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

  /*
    Estos nombres coinciden con el core ESP32 2.0.17
    y con el código de video que ya compilaron.
  */
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;

  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;

  /*
    RGB565 permite mostrar video en color.
    El QR se analiza convirtiendo el mismo frame
    a escala de grises.
  */
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = FRAMESIZE_QVGA;

  config.jpeg_quality = 15;

  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  const esp_err_t errorCamara =
    esp_camera_init(&config);

  if (errorCamara != ESP_OK) {
    detenerConError(
      "No inicio la camara"
    );
  }

  // ---------------------------------------------------
  // 7. Configurar orientación
  // ---------------------------------------------------

  sensor_t* sensor =
    esp_camera_sensor_get();

  if (sensor == nullptr) {
    detenerConError(
      "No se obtuvo el sensor"
    );
  }

  /*
    Esta orientación fue la que permitió leer
    correctamente el QR en las pruebas anteriores.

    También sustituye el espejado manual que tenía
    el código original del video.
  */
  if (
    sensor->set_hmirror(
      sensor,
      1
    ) != 0
  ) {
    detenerConError(
      "No se pudo orientar camara"
    );
  }

  delay(1000);

  estadoPantalla = ESTADO_VIDEO;
  tft.fillScreen(TFT_BLACK);
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  // ---------------------------------------------------
  // 1. Obtener un único fotograma
  // ---------------------------------------------------

  camera_fb_t* fb =
    esp_camera_fb_get();

  if (fb == nullptr) {
    delay(10);
    return;
  }

  // ---------------------------------------------------
  // 2. Preparar imagen de video
  // ---------------------------------------------------

  const bool mostrarVideoAhora =
    estadoPantalla == ESTADO_VIDEO;

  if (mostrarVideoAhora) {
    copiarVideoRecortado(fb);
  }

  // ---------------------------------------------------
  // 3. Decidir si corresponde analizar un QR
  // ---------------------------------------------------

  contadorFramesQR++;

  bool hacerEscaneo = false;

  if (
    contadorFramesQR >=
    FRAMES_ENTRE_ESCANEOS
  ) {
    contadorFramesQR = 0;
    hacerEscaneo = true;
  }

  // ---------------------------------------------------
  // 4. Analizar el mismo fotograma
  // ---------------------------------------------------

  String textoDetectado = "";
  bool qrValido = false;

  if (hacerEscaneo) {
    qrValido = detectarQR(
      fb,
      textoDetectado
    );
  }

  // ---------------------------------------------------
  // 5. Devolver el frame a la cámara
  // ---------------------------------------------------

  esp_camera_fb_return(fb);

  // ---------------------------------------------------
  // 6. Mostrar video
  // ---------------------------------------------------

  if (mostrarVideoAhora) {
    tft.pushImage(
      0,
      0,
      DISP_SIZE,
      DISP_SIZE,
      frameBuffer
    );
  }

  // ---------------------------------------------------
  // 7. Procesar QR válido
  // ---------------------------------------------------

  if (qrValido) {
    procesarQRValido(
      textoDetectado
    );
  }

  const uint32_t ahora = millis();

  // ---------------------------------------------------
  // 8. Pasar de aviso a contenido
  // ---------------------------------------------------

  if (
    estadoPantalla == ESTADO_AVISO_QR &&
    ahora - inicioAvisoQR >=
      DURACION_AVISO_MS
  ) {
    mostrarTextoQR(
      textoQRActual
    );

    estadoPantalla =
      ESTADO_TEXTO_QR;
  }

  // ---------------------------------------------------
  // 9. Volver al video cuando desaparece el QR
  // ---------------------------------------------------

  if (
    estadoPantalla != ESTADO_VIDEO &&
    ahora - ultimaDeteccionQR >=
      TIEMPO_SIN_QR_MS
  ) {
    estadoPantalla =
      ESTADO_VIDEO;

    textoQRActual = "";

    /*
      Queda negro durante un instante y el siguiente
      fotograma vuelve a mostrar el video.
    */
    tft.fillScreen(TFT_BLACK);
  }

  delay(1);
}