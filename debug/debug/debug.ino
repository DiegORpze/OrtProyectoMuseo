#include "esp_camera.h"
#include <quirc.h>

// =====================================================
// Pines de cámara ESP32-CAM AI-Thinker
// =====================================================

#define PWDN_GPIO_NUM      32
#define RESET_GPIO_NUM     -1
#define XCLK_GPIO_NUM       0
#define SIOD_GPIO_NUM      26
#define SIOC_GPIO_NUM      27

#define Y9_GPIO_NUM        35
#define Y8_GPIO_NUM        34
#define Y7_GPIO_NUM        39
#define Y6_GPIO_NUM        36
#define Y5_GPIO_NUM        21
#define Y4_GPIO_NUM        19
#define Y3_GPIO_NUM        18
#define Y2_GPIO_NUM         5

#define VSYNC_GPIO_NUM     25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// =====================================================
// Variables del lector QR
// =====================================================

struct quirc *qrScanner = nullptr;

unsigned long lastStatusTime = 0;
unsigned long frameCounter = 0;
unsigned long qrCounter = 0;

// =====================================================
// Inicialización de la cámara
// =====================================================

bool setupCamera() {
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
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.frame_size = FRAMESIZE_QVGA;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  Serial.println();
  Serial.println("Inicializando camara...");

  esp_err_t error = esp_camera_init(&config);

  if (error != ESP_OK) {
    Serial.printf("ERROR al inicializar la camara: 0x%X\n", error);
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();

  if (sensor != nullptr) {
    sensor->set_framesize(sensor, FRAMESIZE_QVGA);
    sensor->set_brightness(sensor, 0);
    sensor->set_contrast(sensor, 0);
    sensor->set_saturation(sensor, 0);
  }

  Serial.println("Camara inicializada correctamente.");
  return true;
}

// =====================================================
// Inicializar quirc UNA SOLA VEZ al inicio
// =====================================================

bool initQuirc(int width, int height) {
  if (qrScanner == nullptr) {
    Serial.println("ERROR: qrScanner es nullptr.");
    return false;
  }

  Serial.printf("Configurando quirc para %d x %d pixeles...\n", width, height);

  int result = quirc_resize(qrScanner, width, height);

  if (result < 0) {
    Serial.println("ERROR: quirc_resize fallo.");
    Serial.printf("Heap libre: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("PSRAM libre: %u bytes\n", ESP.getFreePsram());
    return false;
  }

  Serial.println("quirc configurado correctamente.");
  return true;
}

// =====================================================
// Mostrar contenido del QR — SOLO texto legible ASCII
// =====================================================

void printQRData(const struct quirc_data &data, int index) {
  qrCounter++;

  Serial.println();
  Serial.println("========================================");
  Serial.printf("QR DECODIFICADO #%lu\n", qrCounter);
  Serial.printf("Indice dentro del frame: %d\n", index);
  Serial.printf("Cantidad de bytes: %d\n", data.payload_len);

  // Verificar si todos los bytes son ASCII legibles (32-126)
  bool allPrintable = true;
  for (int i = 0; i < data.payload_len; i++) {
    uint8_t c = data.payload[i];
    if (c < 32 || c >= 127) {
      allPrintable = false;
      break;
    }
  }

  // Solo imprime texto legible
  Serial.print("Texto legible: [");
  if (allPrintable && data.payload_len > 0) {
    for (int i = 0; i < data.payload_len; i++) {
      Serial.print((char)data.payload[i]);
    }
  } else {
    Serial.print("(datos binarios - no es texto legible)");
  }
  Serial.println("]");

  // Hex para diagnostico
  Serial.print("Bytes HEX: ");
  for (int i = 0; i < data.payload_len; i++) {
    if (data.payload[i] < 16) Serial.print("0");
    Serial.print(data.payload[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  if (!allPrintable) {
    Serial.println("AVISO: contiene bytes no imprimibles - probable falso positivo");
  }

  Serial.println("========================================");
  Serial.println();
}

// =====================================================
// Escanear un frame
// =====================================================

void scanFrame(camera_fb_t *frame) {
  if (frame == nullptr) {
    Serial.println("ERROR: frame nulo.");
    return;
  }

  if (frame->format != PIXFORMAT_GRAYSCALE) {
    Serial.printf("ERROR: formato inesperado de imagen: %d\n", frame->format);
    return;
  }

  int width = frame->width;
  int height = frame->height;
  size_t expectedBytes = width * height;

  Serial.printf("Frame: %dx%d, %u bytes\n", width, height, (unsigned int)frame->len);

  if (frame->len < expectedBytes) {
    Serial.println("ERROR: el framebuffer es mas pequeno de lo esperado.");
    return;
  }

  if (frame->buf == nullptr) {
    Serial.println("ERROR: frame->buf es nullptr.");
    return;
  }

  // quirc_begin devuelve un puntero al buffer interno
  int quircW = 0, quircH = 0;
  uint8_t *quircBuffer = quirc_begin(qrScanner, &quircW, &quircH);

  if (quircBuffer == nullptr) {
    Serial.println("ERROR: quirc_begin devolvio nullptr.");
    return;
  }

  if (quircW != width || quircH != height) {
    Serial.printf("WARN: dimensiones no coinciden. Frame: %dx%d, quirc: %dx%d\n",
      width, height, quircW, quircH);
    quirc_end(qrScanner);
    return;
  }

  // Copiar el frame al buffer de quirc
  memcpy(quircBuffer, frame->buf, expectedBytes);

  // Analizar y buscar QR
  quirc_end(qrScanner);

  int qrCount = quirc_count(qrScanner);

  if (qrCount == 0) {
    return;
  }

  Serial.printf("Se encontraron %d posible(s) QR en el frame.\n", qrCount);

  for (int i = 0; i < qrCount; i++) {
    struct quirc_code code;
    struct quirc_data data;

    quirc_extract(qrScanner, i, &code);

    quirc_decode_error_t decodeResult = quirc_decode(&code, &data);

    if (decodeResult == QUIRC_SUCCESS) {
      printQRData(data, i);
    } else {
      Serial.printf("QR #%d encontrado, pero no se pudo decodificar: %s\n",
        i, quirc_strerror(decodeResult));
    }
  }
}

// =====================================================
// Setup
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("========================================");
  Serial.println("PRUEBA ESP32-CAM + QUIRC");
  Serial.println("Solo Serial Monitor - sin pantalla TFT");
  Serial.println("========================================");

  Serial.printf("PSRAM detectada: %s\n", psramFound() ? "SI" : "NO");
  Serial.printf("Heap libre al iniciar: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("PSRAM libre: %u bytes\n", ESP.getFreePsram());

  // Crear quirc
  qrScanner = quirc_new();

  if (qrScanner == nullptr) {
    Serial.println("ERROR CRITICO: quirc_new fallo.");
    while (true) { delay(1000); }
  }

  Serial.println("Objeto quirc creado correctamente.");

  // Inicializar camara
  if (!setupCamera()) {
    Serial.println("No se puede continuar sin camara.");
    while (true) { delay(1000); }
  }

  // Dejar que la camara se estabilice con frames dummy
  Serial.println("Esperando que la camara se estabilice...");
  for (int i = 0; i < 3; i++) {
    camera_fb_t *dummy = esp_camera_fb_get();
    if (dummy) {
      Serial.printf("Frame dummy %d: %dx%d, %u bytes\n",
        i+1, dummy->width, dummy->height, (unsigned int)dummy->len);
      esp_camera_fb_return(dummy);
    } else {
      Serial.printf("Frame dummy %d: NULL\n", i+1);
    }
    delay(100);
  }

  // Inicializar quirc UNA SOLA VEZ con las dimensiones de la camara
  if (!initQuirc(320, 240)) {
    Serial.println("ERROR: no se pudo inicializar quirc.");
    while (true) { delay(1000); }
  }

  Serial.println();
  Serial.println("Sistema preparado.");
  Serial.println("Solo se mostraran resultados con texto legible ASCII.");
  Serial.println("Si ve '(datos binarios)' es un falso positivo.");
}

// =====================================================
// Loop
// =====================================================

void loop() {
  camera_fb_t *frame = esp_camera_fb_get();

  if (frame == nullptr) {
    Serial.println("ERROR: esp_camera_fb_get fallo (frame null).");
    delay(100);
    return;
  }

  frameCounter++;

  scanFrame(frame);

  esp_camera_fb_return(frame);

  // Estado cada 2 segundos
  if (millis() - lastStatusTime >= 2000) {
    lastStatusTime = millis();
    Serial.printf("Buscando QR... Frames: %lu | QR detectados: %lu | Heap: %u | PSRAM: %u\n",
      frameCounter, qrCounter, ESP.getFreeHeap(), ESP.getFreePsram());
  }

  delay(30);
  yield();
}
