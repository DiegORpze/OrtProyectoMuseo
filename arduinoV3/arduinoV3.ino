#include "esp_camera.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <quirc.h>

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

TFT_eSPI tft = TFT_eSPI();

struct quirc *qr = nullptr;

// Shared variables between cores
volatile bool frame_ready_for_scan = false;
uint8_t *grayscale_buf = nullptr; 

// Variables for Core 0 to pass data back to Core 1 safely
volatile bool update_ui = false;
char new_payload[64] = "";
String last_painting_name = "";

// Task handle for Core 0
TaskHandle_t Task0;

void setup() {
  Serial.begin(230400);
  delay(1000);

  // Manual reset
  pinMode(12, OUTPUT);
  digitalWrite(12, LOW);
  delay(50);
  digitalWrite(12, HIGH);
  delay(150);

  tft.init();
  tft.setRotation(2); 
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(false); 

  // Allocate memory safely (try PSRAM first, fall back to normal RAM)
  if(ESP.getPsramSize() > 0) {
    grayscale_buf = (uint8_t *)ps_malloc(320 * 240);
  } else {
    grayscale_buf = (uint8_t *)malloc(320 * 240);
  }
  
  if (!grayscale_buf) {
    Serial.println("Failed to allocate grayscale buffer!");
  }

  // Initialize QR Decoder
  qr = quirc_new();
  if (!qr) {
    Serial.println("Failed to allocate quirc memory");
  } else {
    if (quirc_resize(qr, 320, 240) < 0) {
      Serial.println("Failed to resize quirc");
    }
  }

  camera_config_t config;
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; 
  config.pixel_format = PIXFORMAT_RGB565; 
  config.frame_size = FRAMESIZE_QVGA;
  config.fb_count = 2; 
  config.grab_mode = CAMERA_GRAB_LATEST;

  Serial.println("Initializing camera...");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    tft.fillScreen(TFT_RED);
    tft.drawString("Cam Error", 10, 10, 2);
    Serial.printf("Error: 0x%x\n", err);
    return;
  }

  // Start Core 0 task
  xTaskCreatePinnedToCore(
    scannerTask,   // Task function
    "Scanner",     // Name
    10000,         // Stack size (large for quirc)
    NULL,          // Parameters
    1,             // Priority
    &Task0,        // Task handle
    0              // Pin to Core 0
  );
}

// ------------------- CORE 1: VIDEO & UI LOOP -------------------
void loop() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) return;

  // 1. Draw the live video to the screen immediately
  tft.pushImage(0, 0, fb->width, fb->height, (uint16_t *)fb->buf);

  // 2. If Core 0 is finished, give it a new frame to scan
  if (!frame_ready_for_scan && grayscale_buf) {
    for (int i = 0; i < 320 * 240; i++) {
      uint16_t pixel = (fb->buf[i*2] << 8) | fb->buf[i*2+1]; 
      uint8_t r = (pixel >> 11) & 0x1F;
      uint8_t g = (pixel >> 5) & 0x3F;
      uint8_t b = pixel & 0x1F;
      grayscale_buf[i] = (r * 77 + g * 150 + b * 29) >> 3; 
    }
    frame_ready_for_scan = true;
  }

  // 3. If Core 0 successfully read a QR code, draw it on the screen HERE (Prevents SPI crash)
  if (update_ui) {
    update_ui = false; // Clear flag
    String payload = String(new_payload);
    
    // Debug: Print raw text at the top of the screen so you know it scanned!
    tft.fillRect(0, 0, 240, 20, TFT_WHITE);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.drawString("Read: " + payload, 2, 2, 2);

    // --- MUSEUM LOGIC HERE ---
    if (payload == "MonaLisa") {
      tft.fillRect(0, 210, 240, 30, TFT_BLUE);
      tft.setTextColor(TFT_WHITE, TFT_BLUE);
      tft.drawString("Painting: Mona Lisa", 5, 215, 2);
    }
    else if (payload == "StarryNight") {
      tft.fillRect(0, 210, 240, 30, TFT_RED);
      tft.setTextColor(TFT_WHITE, TFT_RED);
      tft.drawString("Painting: Starry Night", 5, 215, 2);
    }
    else if (payload == "TheScream") {
      tft.fillRect(0, 210, 240, 30, TFT_GREEN);
      tft.setTextColor(TFT_BLACK, TFT_GREEN);
      tft.drawString("Painting: The Scream", 5, 215, 2);
    }
  }

  esp_camera_fb_return(fb);
}

// ------------------- CORE 0: QR SCANNER LOOP -------------------
void scannerTask(void *pvParameters) {
  for (;;) {
    // Wait until Core 1 gives us a new frame to scan
    if (frame_ready_for_scan) {
      
      int w = 0, h = 0;
      uint8_t *qbuf = quirc_begin(qr, &w, &h);
      
      if (qbuf) {
        memcpy(qbuf, grayscale_buf, w * h);
      }
      quirc_end(qr);

      int count = quirc_count(qr);
      if (count > 0) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(qr, 0, &code);
        
        if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
          // Copy the text to the shared variable
          strncpy(new_payload, (const char *)data.payload, 63);
          new_payload[63] = '\0'; // Ensure null-terminated
          
          // Tell Core 1 to update the screen
          update_ui = true; 
        }
      }
      // Tell Core 1 we are done and ready for the next frame
      frame_ready_for_scan = false;
    } else {
      // Yield to prevent hogging the CPU
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
  }
}