#include "esp_camera.h"
#include <ESP32QRCodeReader.h>
#include <TFT_eSPI.h>

// =====================================================
// OBJETOS PRINCIPALES
// =====================================================

TFT_eSPI tft = TFT_eSPI();

ESP32QRCodeReader lectorQR(
  CAMERA_MODEL_AI_THINKER,
  FRAMESIZE_QVGA);

// Guarda el último QR mostrado para no redibujarlo continuamente.
String ultimoTextoMostrado = "";
unsigned long ultimaDeteccionQR = 0;
const unsigned long TIEMPO_SIN_QR = 1500;

bool hayTextoEnPantalla = false;

// =====================================================
// DETENER EL PROGRAMA ANTE UN ERROR CRÍTICO
// =====================================================

void detenerPrograma() {
  /*
    No se imprime nada por el Monitor Serie.

    Si ocurre un error crítico, el programa queda detenido.
    La pantalla permanecerá negra si ya fue inicializada.
  */

  while (true) {
    delay(1000);
  }
}


// =====================================================
// LIMPIAR EL TEXTO RECIBIDO DESDE EL QR
// =====================================================

String obtenerTextoQR(const QRCodeData& datosQR) {
  String texto = "";

  if (datosQR.payloadLen <= 0) {
    return texto;
  }

  /*
    payload tiene un tamaño máximo definido por la biblioteca.
    Se limita la longitud para evitar leer fuera del arreglo.
  */
  size_t longitud = (size_t)datosQR.payloadLen;
  size_t capacidad = sizeof(datosQR.payload);

  if (longitud > capacidad) {
    longitud = capacidad;
  }

  bool espacioPendiente = false;

  for (size_t i = 0; i < longitud; i++) {
    uint8_t caracter = datosQR.payload[i];

    // Fin explícito del texto.
    if (caracter == '\0') {
      break;
    }

    /*
      Los saltos de línea, retornos y tabulaciones se
      convierten en un único espacio.

      Esto corrige casos como:
      MonaLisa\n
    */
    if (
      caracter == '\n' || caracter == '\r' || caracter == '\t') {
      espacioPendiente = true;
      continue;
    }

    // Ignorar otros caracteres de control.
    if (caracter < 32) {
      continue;
    }

    if (espacioPendiente && texto.length() > 0) {
      texto += ' ';
    }

    espacioPendiente = false;
    texto += (char)caracter;
  }

  // Elimina espacios al principio y al final.
  texto.trim();

  return texto;
}


// =====================================================
// MOSTRAR ÚNICAMENTE EL TEXTO DEL QR
// =====================================================

void mostrarTextoQR(const String& texto) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  const int margen = 10;
  const int anchoDisponible = tft.width() - (margen * 2);

  /*
    Se comienza con una fuente grande.
    Si el texto no entra, se reduce automáticamente.
  */
  uint8_t fuente = 4;

  if (tft.textWidth(texto, fuente) > anchoDisponible) {
    fuente = 2;
  }

  if (tft.textWidth(texto, fuente) > anchoDisponible) {
    fuente = 1;
  }

  /*
    Si entra en una sola línea, se muestra centrado
    horizontal y verticalmente.
  */
  if (tft.textWidth(texto, fuente) <= anchoDisponible) {
    tft.setTextDatum(MC_DATUM);

    tft.drawString(
      texto,
      tft.width() / 2,
      tft.height() / 2,
      fuente);

    return;
  }

  /*
    Si incluso con la fuente más pequeña no entra en
    una línea, se activa el ajuste automático de texto.
  */
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextWrap(true, true);
  tft.setCursor(margen, margen, 1);
  tft.print(texto);
}


// =====================================================
// SETUP
// =====================================================

void setup() {
  /*
    Se conserva esta espera porque en las pruebas anteriores
    ayudó a obtener un arranque estable.
  */
  delay(1500);

  // ---------------------------------------------------
  // 1. Verificar PSRAM
  // ---------------------------------------------------

  if (!psramFound()) {
    detenerPrograma();
  }

  // ---------------------------------------------------
  // 2. Secuencia de encendido de la cámara
  // ---------------------------------------------------

  /*
    GPIO32 corresponde a PWDN en la ESP32-CAM AI-Thinker.

    Primero se apaga la cámara y luego se vuelve a encender
    para asegurar un inicio limpio.
  */
  pinMode(32, OUTPUT);

  digitalWrite(32, HIGH);
  delay(100);

  digitalWrite(32, LOW);
  delay(100);

  // ---------------------------------------------------
  // 3. Inicializar cámara y lector QR
  // ---------------------------------------------------

  /*
    Desactiva todos los mensajes internos de diagnóstico
    de ESP32QRCodeReader.
  */
  lectorQR.setDebug(false);

  QRCodeReaderSetupErr resultado = lectorQR.setup();

  if (resultado == SETUP_NO_PSRAM_ERROR) {
    detenerPrograma();
  }

  if (resultado == SETUP_CAMERA_INIT_ERROR) {
    detenerPrograma();
  }

  if (resultado != SETUP_OK) {
    detenerPrograma();
  }

  // ---------------------------------------------------
  // 4. Configurar la orientación de la cámara
  // ---------------------------------------------------

  sensor_t* sensor = esp_camera_sensor_get();

  if (sensor == nullptr) {
    detenerPrograma();
  }

  /*
    Esta orientación fue la que permitió decodificar
    correctamente el QR MonaLisa.
  */
  sensor->set_hmirror(sensor, 1);

  // ---------------------------------------------------
  // 5. Inicializar la pantalla TFT
  // ---------------------------------------------------

  tft.init();

  /*
    Rotación horizontal utilizada por la versión
    que anteriormente funcionó.

    Si queda invertida físicamente, cambiar 3 por 1.
  */
  tft.setRotation(3);

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextWrap(false, false);

  /*
    La pantalla queda completamente negra.
    No se muestran mensajes de bienvenida ni de estado.
  */

  // ---------------------------------------------------
  // 6. Iniciar el escaneo QR
  // ---------------------------------------------------

  /*
    Da tiempo al sensor para estabilizarse antes
    de iniciar la tarea de escaneo.
  */
  delay(1000);

  lectorQR.beginOnCore(1);

  // Asegurar nuevamente que la pantalla quede negra.
  tft.fillScreen(TFT_BLACK);
}


// =====================================================
// LOOP
// =====================================================

void loop() {
  QRCodeData datosQR = {};

  if (lectorQR.receiveQrCode(&datosQR, 100)) {

    if (datosQR.valid) {
      String textoQR = obtenerTextoQR(datosQR);

      if (textoQR.length() > 0) {
        ultimaDeteccionQR = millis();

        if (textoQR != ultimoTextoMostrado || !hayTextoEnPantalla) {
          ultimoTextoMostrado = textoQR;
          mostrarTextoQR(textoQR);
          hayTextoEnPantalla = true;
        }
      }
    }
  }

  if (hayTextoEnPantalla && millis() - ultimaDeteccionQR >= TIEMPO_SIN_QR) {
    tft.fillScreen(TFT_BLACK);
    hayTextoEnPantalla = false;
    ultimoTextoMostrado = "";
  }

  delay(10);
}