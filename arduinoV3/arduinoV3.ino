#include <SPI.h>
#include <TFT_eSPI.h>
#include <ESP32QRCodeReader.h>

TFT_eSPI tft = TFT_eSPI();

// --- Subclass to override camera config before calling parent setup() ---
// ESP32QRCodeReader::setup() hardcodes pixel_format, fb_count, and xclk,
// so we override to inject our desired values before calling the parent.
class CustomESP32QRCodeReader : public ESP32QRCodeReader {
public:
  using ESP32QRCodeReader::ESP32QRCodeReader;  // inherit all constructors

  QRCodeReaderSetupErr setup() override {
    this->cameraConfig.pixel_format = PIXFORMAT_RGB565;  // correct colors for TFT
    this->cameraConfig.fb_count = 2;                     // two buffers: one for QR, one for video
    this->cameraConfig.xclk_freq_hz = 20000000;          // your original 20MHz
    return ESP32QRCodeReader::setup();  // parent does the actual esp_camera_init()
  }
};

CustomESP32QRCodeReader reader(CAMERA_MODEL_AI_THINKER);

// QR result display state
volatile bool display_result = false;
char qr_payload[128] = "";

// Dismiss button: active LOW (wire a button between IO4 and GND)
// If not wired, result auto-dismisses after 5 seconds.
#define DISMISS_PIN 4

void setup() {
  Serial.begin(230400);

  // TFT init
  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setSwapBytes(false);

  // Show init message BEFORE camera setup
  tft.drawString("Camera Initializing...", 10, 110, 2);
  Serial.println("Camera Initializing...");

  // Camera + QR library init (uses overridden setup() above)
  QRCodeReaderSetupErr err = reader.setup();
  if (err != SETUP_OK) {
    tft.fillScreen(TFT_RED);
    if (err == SETUP_NO_PSRAM_ERROR) {
      tft.drawString("No PSRAM!", 10, 10, 2);
    } else {
      tft.drawString("Camera Error!", 10, 10, 2);
    }
    Serial.printf("Camera setup error: %d\n", err);
    while (1) { delay(1000); }  // halt
  }

  // Dismiss button (active low, internal pullup)
  pinMode(DISMISS_PIN, INPUT_PULLUP);

  // QR scanning task runs on Core 0 internally
  reader.beginOnCore(0);

  Serial.println("Setup complete - scanning for QR codes");
}

void loop() {
  // --- Show QR result when detected ---
  if (display_result) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(qr_payload, 10, 110, 4);

    Serial.print("QR detected: ");
    Serial.println(qr_payload);

    // Wait: dismiss button (IO4 to GND) or 5 seconds, then resume video
    unsigned long start = millis();
    while (display_result) {
      if (digitalRead(DISMISS_PIN) == LOW) {
        delay(50);  // debounce
        if (digitalRead(DISMISS_PIN) == LOW) {
          display_result = false;
        }
      }
      if (millis() - start > 5000) {
        display_result = false;
      }
      delay(50);
    }

    Serial.println("Resuming video...");
    return;  // restart the video loop cleanly
  }

  // --- Continuous video feed ---
  struct QRCodeData qrCodeData;

  // Poll for QR code (non-blocking, timeout=0 returns immediately if queue is empty)
  if (reader.receiveQrCode(&qrCodeData, 0)) {
    if (qrCodeData.valid) {
      int len = qrCodeData.payloadLen;
      if (len > 127) len = 127;
      memcpy(qr_payload, qrCodeData.payload, len);
      qr_payload[len] = '\0';
      display_result = true;
    }
  }

  // Try to grab a frame and push it to TFT
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb) {
    tft.pushImage(0, 0, fb->width, fb->height, (uint16_t *)fb->buf);
    esp_camera_fb_return(fb);
  }
}
