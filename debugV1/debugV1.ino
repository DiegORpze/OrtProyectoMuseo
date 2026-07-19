#include "esp_camera.h"
#include <ESP32QRCodeReader.h>

// ESP32-CAM modelo AI-Thinker.
// QVGA = 320 x 240, suficiente para el test y menos exigente en memoria.
ESP32QRCodeReader lectorQR(
  CAMERA_MODEL_AI_THINKER,
  FRAMESIZE_QVGA);

void detenerPrograma(const char *mensaje) {
  Serial.println();
  Serial.println("================================");
  Serial.println("ERROR");
  Serial.println(mensaje);
  Serial.println("Programa detenido.");
  Serial.println("================================");

  while (true) {
    delay(1000);
  }
}

void setup() {
  // El Monitor Serie debe configurarse también en 115200.
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("================================");
  Serial.println("PRUEBA DE LECTURA DE CODIGOS QR");
  Serial.println("ESP32-CAM AI-THINKER");
  Serial.println("================================");

  // La biblioteca necesita PSRAM.
  if (!psramFound()) {
    detenerPrograma(
      "No se detecto PSRAM.");
  }

  Serial.println("PSRAM detectada.");
  Serial.println("Inicializando camara...");

  lectorQR.setDebug(false);

  QRCodeReaderSetupErr resultado = lectorQR.setup();

  if (resultado == SETUP_NO_PSRAM_ERROR) {
    detenerPrograma(
      "La biblioteca no encontro PSRAM.");
  }

  if (resultado == SETUP_CAMERA_INIT_ERROR) {
    detenerPrograma(
      "No se pudo inicializar la camara.");
  }

  if (resultado != SETUP_OK) {
    detenerPrograma(
      "Error desconocido al iniciar el lector.");
  }

  /*
    Inicia la tarea interna que captura imágenes
    y busca códigos QR.
  */
  sensor_t *sensor = esp_camera_sensor_get();

  if (sensor == nullptr) {
    detenerPrograma(
      "No se pudo obtener el sensor de la camara.");
  }

  int resultadoEspejo = sensor->set_hmirror(sensor, 1);

  if (resultadoEspejo == 0) {
    Serial.println("Imagen horizontalmente invertida para prueba.");
  } else {
    Serial.println("No se pudo cambiar la orientacion horizontal.");
  }

  delay(1000);
  lectorQR.beginOnCore(1);

  Serial.println("Camara inicializada correctamente.");
  Serial.println("Lector QR preparado.");
  Serial.println();
  Serial.println("Coloque un codigo QR frente a la camara.");
  Serial.println();
}

void loop() {
  QRCodeData datosQR = {};

  /*
    Comprueba durante un máximo de 100 ms
    si la tarea de lectura encontró un QR.
  */
  if (lectorQR.receiveQrCode(&datosQR, 100)) {
    Serial.println();
    Serial.println("--------------------------------");

    if (datosQR.valid) {
      Serial.print("CONTENIDO DEL QR: [");

      /*
        Imprime exactamente la cantidad de bytes
        que contiene el QR.
      */
      Serial.write(datosQR.payload, datosQR.payloadLen);

      Serial.println("]");

      Serial.print("Longitud: ");
      Serial.print(datosQR.payloadLen);
      Serial.println(" bytes");
    } else {
      Serial.println(
        "Se encontro un posible QR, pero no pudo decodificarse.");
    }

    Serial.println("--------------------------------");

    /*
      Evita llenar demasiado rápido el Monitor Serie
      si el QR continúa delante de la cámara.
    */
    delay(1000);
  }

  delay(10);
}