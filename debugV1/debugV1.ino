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

  // CAMERA POWER-ON SEQUENCE FOR ESP32-CAM AI-THINKER
  // GPIO 32 is PWDN for the camera
  pinMode(32, OUTPUT);
  digitalWrite(32, HIGH);  // Power down camera first
  delay(100);
  digitalWrite(32, LOW);   // Power up camera
  delay(100);
  Serial.println("Camera PWDN sequence done.");

  // Initialize camera
  lectorQR.setDebug(false);

  QRCodeReaderSetupErr resultado = lectorQR.setup();  

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

  // Welcome screen - centered for rotation 3
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("PRUEBA DE LECTURA", 160, 170, 2);
  tft.drawString("DE CODIGOS QR", 160, 200, 2);
  delay(2000);
  tft.fillScreen(TFT_BLACK);

  // Show PSRAM status - centered for rotation 3
  tft.setTextDatum(TC_DATUM);
  tft.drawString("PSRAM detectada.", 160, 200, 2);
  delay(500);

  tft.drawString("Camara inicializada", 160, 140, 2);
  tft.drawString("correctamente.", 160, 170, 2);
  delay(1000);

  // Final ready screen - centered for rotation 3
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Lector QR preparado.", 160, 120, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Coloque un codigo QR", 160, 140, 2);
  tft.drawString("frente a la camara.", 160, 170, 2);
}

void loop() {
  QRCodeData datosQR = {};

  // Check for QR code with 100ms timeout
  if (lectorQR.receiveQrCode(&datosQR, 100)) {
    tft.fillScreen(TFT_BLACK);
    tft.setFreeFont(&FreeMono9pt7b);

    /* Draw a border - centered for rotation 3
    tft.drawRect(2, 2, 316, 236, TFT_WHITE);
    tft.drawLine(10, 30, 310, 30, TFT_WHITE);*/

    if (datosQR.valid) {
      // QR Content header - centered
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.drawString("QR DETECTADO", 160, 200, 2);

      // Content label - left aligned for readability
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextDatum(TL_DATUM);
      tft.setCursor(15, 180);
      tft.print("CONTENIDO: ");

      // Print the QR payload bytes in yellow
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      for (size_t i = 0; i < datosQR.payloadLen; i++) {
        tft.print((char)datosQR.payload[i]);
      }

      // Show length in white
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString("Longitud: ", 160, 140, 2);
      tft.print(datosQR.payloadLen);
      tft.print(" bytes");

      /* Draw separator
      tft.drawLine(10, 130, 310, 130, TFT_WHITE);*/

      // Show raw payload in cyan
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.setCursor(2, 160);
      tft.print("[");
      for (size_t i = 0; i < datosQR.payloadLen; i++) {
        tft.print((char)datosQR.payload[i]);
      }
      tft.print("]");

    } else {
      // Invalid QR found - centered
      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.drawString("POSIBLE QR", 160, 200, 2);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextDatum(TL_DATUM);
      tft.drawString("Se encontro un posible QR,", 120, 140, 2);
      tft.drawString("pero no pudo decodificarse.", 120, 160, 2);
    }

    // Delay before next scan
    delay(1000);
  }

  delay(10);
}
