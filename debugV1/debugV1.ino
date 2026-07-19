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

// Display state
bool pantallaInicializada = false;

// Helper: show message on TFT centered, with delay
void mostrarMensaje(const char* linea1, const char* linea2 = nullptr, uint32_t retardo = 1500) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);  // Base text size, font 2 will be used

  // Use Font 2 (16px height) for readability on 320x240
  tft.setFreeFont(&FreeMono9pt7b);

  int16_t y = 60;

  // Center first line
  int16_t x1_1, y1_1, x2_1, y2_1;
  tft.setTextDatum(CC_DATUM);  // Center-center datum
  tft.getTextBounds(linea1, 0, 0, &x1_1, &y1_1, &x2_1, &y2_1);
  tft.drawString(linea1, 160, y, GFXFF);

  if (linea2 != nullptr) {
    y = 110;
    int16_t x1_2, y1_2, x2_2, y2_2;
    tft.getTextBounds(linea2, 0, 0, &x1_2, &y1_2, &x2_2, &y2_2);
    tft.drawString(linea2, 160, y, GFXFF);
  }

  delay(retardo);
}

void mostrarError(const char* mensaje) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(CC_DATUM);

  tft.drawString("================================", 160, 70, GFXFF);
  tft.drawString("ERROR", 160, 100, GFXFF);
  tft.drawString(mensaje, 160, 130, GFXFF);
  tft.drawString("Programa detenido.", 160, 160, GFXFF);
  tft.drawString("================================", 160, 190, GFXFF);

  while (true) {
    delay(1000);
  }
}

void mostrarLinea(const char* mensaje) {
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TL_DATUM);  // Top-left datum
  tft.println(mensaje);
}

void setup() {
  // Initialize TFT display first
  tft.init();
  tft.setRotation(1);  // Landscape orientation for 320x240
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setFreeFont(&FreeMono9pt7b);

  // Welcome screen
  tft.setTextDatum(CC_DATUM);
  tft.drawString("PRUEBA DE LECTURA", 160, 80, GFXFF);
  tft.drawString("DE CODIGOS QR", 160, 110, GFXFF);
  delay(2000);
  tft.fillScreen(TFT_BLACK);

  // The Monitor Serie must also be configured at 115200 for camera lib.
  Serial.begin(115000);
  delay(500);

  // Show setup steps on screen
  tft.setTextDatum(TL_DATUM);

  // Step 1: PSRAM check
  tft.println("Iniciando...");
  delay(500);

  // The library needs PSRAM.
  if (!psramFound()) {
    mostrarError("No se detecto PSRAM.");
  }

  tft.println("PSRAM detectada.");
  delay(500);
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
    tft.println("Imagen horizontalmente");
    tft.println("invertida para prueba.");
  } else {
    tft.println("No se pudo cambiar la");
    tft.println("orientacion horizontal.");
  }

  delay(1000);
  lectorQR.beginOnCore(1);

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.drawString("Camara inicializada", 160, 80, GFXFF);
  tft.drawString("correctamente.", 160, 105, GFXFF);
  delay(1500);

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(CC_DATUM);
  tft.drawString("Lector QR preparado.", 160, 90, GFXFF);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Coloque un codigo QR", 160, 140, GFXFF);
  tft.drawString("frente a la camara.", 160, 160, GFXFF);

  pantallaInicializada = true;
}

void loop() {
  QRCodeData datosQR = {};

  /*
    Comprueba durante un maximo de 100 ms
    si la tarea de lectura encontro un QR.
  */
  if (lectorQR.receiveQrCode(&datosQR, 100)) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setFreeFont(&FreeMono9pt7b);
    tft.setTextDatum(TL_DATUM);

    // Draw a border
    tft.drawRect(2, 2, 236, 296, TFT_WHITE);
    tft.drawRect(4, 4, 232, 292, TFT_WHITE);

    // Separator line
    tft.drawLine(10, 30, 230, 30, TFT_WHITE);

    if (datosQR.valid) {
      // QR Content header
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("QR DETECTADO", 160, 10, GFXFF);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);

      // Show "CONTENIDO DEL QR:"
      tft.setCursor(15, 45);
      tft.print("CONTENIDO: ");

      // Print the QR payload bytes
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      for (size_t i = 0; i < datosQR.payloadLen; i++) {
        tft.print((char)datosQR.payload[i]);
      }

      // Show length
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(15, 75);
      tft.print("Longitud: ");
      tft.print(datosQR.payloadLen);
      tft.print(" bytes");

      // Draw separator
      tft.drawLine(10, 100, 230, 100, TFT_WHITE);

      // Show raw payload for debugging
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.setCursor(15, 115);
      tft.print("[");
      for (size_t i = 0; i < datosQR.payloadLen; i++) {
        tft.print((char)datosQR.payload[i]);
      }
      tft.print("]");

    } else {
      // Invalid QR found
      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.drawString("POSIBLE QR", 160, 10, GFXFF);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(15, 50);
      tft.println("Se encontro un posible QR,");
      tft.setCursor(15, 70);
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
