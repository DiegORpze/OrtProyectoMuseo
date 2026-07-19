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

int quircWidth = 0;
int quircHeight = 0;

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
// Ajustar quirc al tamaño real del frame
// =====================================================

bool prepareQuirc(int width, int height) {
  if (qrScanner == nullptr) {
    Serial.println("ERROR: qrScanner es nullptr.");
    return false;
  }

  if (quircWidth == width && quircHeight == height) {
    return true;
  }

  Serial.printf("Configurando quirc para %d x %d pixeles...\n", width, height);

  int result = quirc_resize(qrScanner, width, height);

  if (result < 0) {
    Serial.println("ERROR: quirc_resize fallo.");
    Serial.printf("Heap libre: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("PSRAM libre: %u bytes\n", ESP.getFreePsram());
    return false;
  }

  quircWidth = width;
  quircHeight = height;

  Serial.println("quirc configurado correctamente.");
  return true;
}

// =====================================================
// Mostrar el contenido del QR — SOLO texto legible
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

  // Solo imprimir si es texto legible
  Serial.print("Texto legible: [");
  if (allPrintable && data.payload_len > 0) {
    for (int i = 0; i < data.payload_len; i++) {
      Serial.print((char)data.payload[i]);
    }
  } else {
    Serial.print("(datos binarios - no es texto legible)");
  }
  Serial.println("]");

  // Mostrar hex para diagnostico
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

<<<<<<< HEAD
  int width = frame->width;
  int height = frame->height;
  size_t expectedBytes = width * height;

  if (frame->len < expectedBytes) {
    Serial.println("ERROR: el framebuffer es mas pequeno de lo esperado.");
    Serial.printf("Recibido: %u bytes\n", (unsigned int)frame->len);
    Serial.printf("Esperado: %u bytes\n", (unsigned int)expectedBytes);
    return;
=======
  // 3. If Core 0 successfully read a NEW QR code, update our text variable
  if (update_ui) {
    update_ui = false; // Clear flag
    String payload = String(new_payload);
    
    // --- MUSEUM LOGIC HERE ---
    if (payload == "MonaLisa") {
      current_painting_text = "Painting: Mona Lisa";
    }
    else if (payload == "StarryNight") {
      current_painting_text = "Painting: Starry Night";
    }
    else if (payload == "TheScream") {
      current_painting_text = "Painting: The Scream";
    }
    else {
      // If it reads a QR code you didn't program, show the raw text
      current_painting_text = "Read: " + payload;
    }
>>>>>>> parent of 3f94afa (VERSION ORIGINAL SAVE)
  }

  if (!prepareQuirc(width, height)) {
    return;
  }

  int scannerWidth = 0;
  int scannerHeight = 0;

  uint8_t *quircBuffer = quirc_begin(qrScanner, &scannerWidth, &scannerHeight);

<<<<<<< HEAD
  if (quircBuffer == nullptr) {
    Serial.println("ERROR: quirc_begin devolvio nullptr.");
    return;
  }

  if (scannerWidth != width || scannerHeight != height) {
    Serial.println("ERROR: las dimensiones de quirc no coinciden.");
    Serial.printf("Frame: %d x %d | quirc: %d x %d\n", width, height, scannerWidth, scannerHeight);
    quirc_end(qrScanner);
    return;
  }

  memcpy(quircBuffer, frame->buf, expectedBytes);

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
=======
      int count = quirc_count(qr);
      if (count > 0) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(qr, 0, &code);
        
        if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
          String payload = String((const char *)data.payload);
          
          // Only trigger a UI update if it's a different QR code than last time
          if (payload != last_painting_name) {
            last_painting_name = payload;
            strncpy(new_payload, (const char *)data.payload, 63);
            new_payload[63] = '\0'; 
            update_ui = true; 
          }
        }
      }
      // Tell Core 1 we are done and ready for the next frame
      frame_ready_for_scan = false;
>>>>>>> parent of 3f94afa (VERSION ORIGINAL SAVE)
    } else {
      Serial.printf("QR #%d encontrado, pero no se pudo decodificar: %s\n", i, quirc_strerror(decodeResult));
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

  qrScanner = quirc_new();

  if (qrScanner == nullptr) {
    Serial.println("ERROR CRITICO: quirc_new fallo.");
    while (true) { delay(1000); }
  }

  Serial.println("Objeto quirc creado correctamente.");

  if (!setupCamera()) {
    Serial.println("No se puede continuar sin camara.");
    while (true) { delay(1000); }
  }

  Serial.println();
  Serial.println("Sistema preparado.");
  Serial.println("Coloque un codigo QR frente a la camara.");
  Serial.println("Solo se mostraran resultados con texto legible ASCII.\n");
}

// =====================================================
// Loop
// =====================================================

void loop() {
  camera_fb_t *frame = esp_camera_fb_get();

  if (frame == nullptr) {
    Serial.println("ERROR: esp_camera_fb_get fallo.");
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
