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
  // Initialize TFT display first
  tft.init();
  tft.setRotation(1);  // Landscape orientation for 320x240
  tft.fillScreen(TFT_BLACK);

  // Set default font and colors
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TL_DATUM);

  // Welcome screen
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("PRUEBA DE LECTURA", 160, 80, 2);
  tft.drawString("DE CODIGOS QR", 160, 110, 2);
  delay(2000);
  tft.fillScreen(TFT_BLACK);

  // The Monitor Serie must also be configured at 115200 for camera lib.
  Serial.begin(115200);
  delay(500);

  // Show setup steps on screen using left-aligned text
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TL_DATUM);

  // Step 1: PSRAM check
  tft.setCursor(10, 10);
  tft.println("Iniciando...");
  delay(500);

  // The library needs PSRAM.
  if (!psramFound()) {
    mostrarError("No se detecto PSRAM.");
  }

  tft.setCursor(10, 30);
  tft.println("PSRAM detectada.");
  delay(500);

  tft.setCursor(10, 50);
  tft.println("Inicializando camara...");
  delay(500);

  lectorQR.setDebug(false);

  QRCodeReaderSetupErr resultado = lectorQR.setup();

  if (resultado == SETUP_NO_PSRAM_ERROR) {
    mostrarError("La biblioteca no encontro PSRAM.");
  }

  if (resultado == SETUP_CAMERA_INIT_ERROR) {
    mostrarError("No se pudo inicializar la camara.");
  }

  if (resultado != SETUP_OK) {
    mostrarError("Error desconocido al iniciar el lector.");
  }

  tft.setCursor(10, 70);
  tft.println("Configurando sensor...");
  delay(500);

  /*
    Inicia la tarea interna que captura imagenes
    y busca codigos QR.
  */
  sensor_t *sensor = esp_camera_sensor_get();

  if (sensor == nullptr) {
    mostrarError("No se pudo obtener el sensor de la camara.");
  }

  int resultadoEspejo = sensor->set_hmirror(sensor, 1);

  if (resultadoEspejo == 0) {
    tft.setCursor(10, 90);
    tft.println("Imagen horizontalmente");
    tft.setCursor(10, 108);
    tft.println("invertida para prueba.");
  } else {
    tft.setCursor(10, 90);
    tft.println("No se pudo cambiar la");
    tft.setCursor(10, 108);
    tft.println("orientacion horizontal.");
  }

  delay(1000);
  lectorQR.beginOnCore(1);

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Camara inicializada", 160, 80, 2);
  tft.drawString("correctamente.", 160, 105, 2);
  delay(1500);

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Lector QR preparado.", 160, 90, 2);
  tft.drawString("Coloque un codigo QR", 160, 130, 2);
  tft.drawString("frente a la camara.", 160, 150, 2);
}

void loop() {
  QRCodeData datosQR = {};

  /*
    Comprueba durante un maximo de 100 ms
    si la tarea de lectura encontro un QR.
  */
  if (lectorQR.receiveQrCode(&datosQR, 100)) {
    tft.fillScreen(TFT_BLACK);
    tft.setFreeFont(&FreeMono9pt7b);
    tft.setTextDatum(TL_DATUM);

    // Draw a border
    tft.drawRect(2, 2, 316, 236, TFT_WHITE);
    tft.drawLine(10, 28, 310, 28, TFT_WHITE);

    if (datosQR.valid) {
      // QR Content header
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.drawString("QR DETECTADO", 160, 8, 2);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextDatum(TL_DATUM);

      // Show "CONTENIDO:"
      tft.setCursor(15, 40);
      tft.print("CONTENIDO: ");

      // Print the QR payload bytes in yellow
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      for (size_t i = 0; i < datosQR.payloadLen; i++) {
        tft.print((char)datosQR.payload[i]);
      }

      // Show length in white
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(15, 68);
      tft.print("Longitud: ");
      tft.print(datosQR.payloadLen);
      tft.print(" bytes");

      // Draw separator
      tft.drawLine(10, 95, 310, 95, TFT_WHITE);

      // Show raw payload in cyan
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.setCursor(15, 108);
      tft.print("[");
      for (size_t i = 0; i < datosQR.payloadLen; i++) {
        tft.print((char)datosQR.payload[i]);
      }
      tft.print("]");

    } else {
      // Invalid QR found
      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.drawString("POSIBLE QR", 160, 8, 2);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextDatum(TL_DATUM);
      tft.setCursor(15, 45);
      tft.println("Se encontro un posible QR,");
      tft.setCursor(15, 63);
      tft.println("pero no pudo decodificarse.");
    }

    /*
      Evita llenar demasiado rapido el display
      si el QR continua delante de la camara.
    */
    delay(1000);
  }

  delay(10);
}
