#include "esp_camera.h"
#include <ESP32QRCodeReader.h>
#include <TFT_eSPI.h>  // TFT_eSPI library

// Initialize TFT display
TFT_eSPI tft = TFT_eSPI();

// ESP32-CAM modelo AI-Thinker.
// QVGA = 320 x 240, suficiente para el test y menos exigente en memoria.
ESP32QRCodeReader lectorQR(
  CAMERA_MODEL_AI_THINKER,
  FRAMESIZE_QVGA);

// Show centered title with specific font number (font 2 = 16px height)
void tituloCentradoFont(const char* str, int y, uint8_t font) {
  tft.setTextDatum(TC_DATUM);
  tft.drawString(str, 160, y, font);
}

void mostrarError(const char* mensaje) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TC_DATUM);

  tituloCentradoFont("================================", 70, 2);
  tituloCentradoFont("ERROR", 95, 2);
  tituloCentradoFont(mensaje, 125, 2);
  tituloCentradoFont("Programa detenido.", 155, 2);
  tituloCentradoFont("================================", 185, 2);

  while (true) {
    delay(1000);
  }
}

void setup() {
  // ============================================
  // STEP 1: Initialize Serial and Camera FIRST
  // ============================================
  Serial.begin(115200);
  delay(1500);

  Serial.println("Iniciando...");

  // Check PSRAM
  if (!psramFound()) {
    Serial.println("ERROR: No se detecto PSRAM.");
    // Can't show on TFT yet
    while (true) delay(1000);
  }
  Serial.println("PSRAM detectada.");

  // CAMERA POWER-ON SEQUENCE FOR ESP32-CAM AI-THINKER
  // GPIO 32 is PWDN for the camera
  pinMode(32, OUTPUT);
  digitalWrite(32, HIGH);  // Power down camera first
  delay(100);
  digitalWrite(32, LOW);   // Power up camera
  delay(100);
  Serial.println("Camera PWDN sequence done.");

  // Initialize camera
  Serial.println("Inicializando camara...");
  lectorQR.setDebug(false);

  QRCodeReaderSetupErr resultado = lectorQR.setup();
  Serial.println("Setup() retorno.");

  if (resultado == SETUP_NO_PSRAM_ERROR) {
    Serial.println("ERROR: La biblioteca no encontro PSRAM.");
    while (true) delay(1000);
  }

  if (resultado == SETUP_CAMERA_INIT_ERROR) {
    Serial.println("ERROR: No se pudo inicializar la camara.");
    while (true) delay(1000);
  }

  if (resultado != SETUP_OK) {
    Serial.println("ERROR: Error desconocido al iniciar el lector.");
    while (true) delay(1000);
  }

  Serial.println("Configurando sensor...");

  // Get sensor and configure mirror
  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor == nullptr) {
    Serial.println("ERROR: No se pudo obtener el sensor.");
    while (true) delay(1000);
  }

  int resultadoEspejo = sensor->set_hmirror(sensor, 1);
  if (resultadoEspejo == 0) {
    Serial.println("Imagen invertida OK.");
  } else {
    Serial.println("Error al invertir imagen.");
  }

  delay(1000);
  lectorQR.beginOnCore(1);
  Serial.println("Camara inicializada correctamente.");

  // ============================================
  // STEP 2: Now initialize TFT display
  // ============================================
  tft.init();
  tft.setRotation(1);  // Landscape orientation for 320x240
  tft.fillScreen(TFT_BLACK);

  // Set default font and colors
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TL_DATUM);

  // ============================================
  // STEP 3: Display status messages on TFT
  // ============================================

  // Welcome screen (rotation 3 = 180° flip, adjust Y coordinates)
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("PRUEBA DE LECTURA", 160, 40, 2);
  tft.drawString("DE CODIGOS QR", 160, 70, 2);
  delay(2000);
  tft.fillScreen(TFT_BLACK);

  // Show PSRAM status - centered
  tft.setTextDatum(TC_DATUM);
  tft.drawString("PSRAM detectada.", 160, 210, 2);
  delay(500);

  tft.drawString("Camara inicializada", 160, 185, 2);
  tft.drawString("correctamente.", 160, 160, 2);
  delay(1000);

  // Final ready screen
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Lector QR preparado.", 160, 150, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Coloque un codigo QR", 160, 110, 2);
  tft.drawString("frente a la camara.", 160, 90, 2);
}

void loop() {
  QRCodeData datosQR = {};

  // Check for QR code with 100ms timeout
  if (lectorQR.receiveQrCode(&datosQR, 100)) {
    tft.fillScreen(TFT_BLACK);
    tft.setFreeFont(&FreeMono9pt7b);
    tft.setTextDatum(TL_DATUM);

    // Draw a border
    tft.drawRect(2, 2, 316, 236, TFT_WHITE);
    tft.drawLine(10, 210, 310, 210, TFT_WHITE);

    if (datosQR.valid) {
      // QR Content header
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.drawString("QR DETECTADO", 160, 230, 2);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextDatum(TL_DATUM);

      // Show "CONTENIDO:"
      tft.setCursor(15, 195);
      tft.print("CONTENIDO: ");

      // Print the QR payload bytes in yellow
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      for (size_t i = 0; i < datosQR.payloadLen; i++) {
        tft.print((char)datosQR.payload[i]);
      }

      // Show length in white
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(15, 170);
      tft.print("Longitud: ");
      tft.print(datosQR.payloadLen);
      tft.print(" bytes");

      // Draw separator
      tft.drawLine(10, 145, 310, 145, TFT_WHITE);

      // Show raw payload in cyan
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.setCursor(15, 130);
      tft.print("[");
      for (size_t i = 0; i < datosQR.payloadLen; i++) {
        tft.print((char)datosQR.payload[i]);
      }
      tft.print("]");

    } else {
      // Invalid QR found
      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.drawString("POSIBLE QR", 160, 230, 2);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextDatum(TL_DATUM);
      tft.setCursor(15, 190);
      tft.println("Se encontro un posible QR,");
      tft.setCursor(15, 172);
      tft.println("pero no pudo decodificarse.");
    }

    // Delay before next scan
    delay(1000);
  }

  delay(10);
}
