// Add ESP32 Boards: First, ensure the ESP32 board package is installed in your Arduino IDE.
// Select Board: Go to Tools > Board > ESP32 Arduino > ESP32S3 Dev Module.
// USB CDC On Boot: Set to Enabled to see serial monitor output (like IP address) via the USB-C port.
// Flash Size: Select 16MB Flash Size.
// Partition Scheme: Choose a scheme that supports PSRAM, such as Huge APP (3MB No OTA/1MB SPIFFS) or similar.
// PSRAM (If Available): Ensure OPI PSRAM is selected for better performance with larger models.
// Port: Select the correct COM port for your device. 

#include "esp_camera.h"
#include "FS.h"
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>

// --- CONFIGURATION ---
#define LED_PIN           3
#define TIME_TO_SLEEP     8      // Seconds between photos
#define CAPTURE_DURATION_MIN 180 // 3 Hours (for the flight)

// Camera Pin Mapping (DFRobot AI Cam S3)
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     5
#define Y9_GPIO_NUM       4
#define Y8_GPIO_NUM       6
#define Y7_GPIO_NUM       7
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       17
#define Y4_GPIO_NUM       21
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM       16
#define VSYNC_GPIO_NUM    1
#define HREF_GPIO_NUM     2
#define PCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM     8
#define SIOC_GPIO_NUM     9

// Persistent variables for flight
RTC_DATA_ATTR int photo_count = 0;
RTC_DATA_ATTR int session_id = 0;

WebServer server(80);

// --- FUNCTIONS ---

void flashLED(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

bool initCamera() {
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
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 12000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_UXGA; // 5MP
  config.jpeg_quality = 10;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) return false;

  sensor_t * s = esp_camera_sensor_get();

  // --- OV5640 IMAGE QUALITY TUNING ---
  s->set_wb_mode(s, 1);          // Fixed to "Sunny" (stops color shifting)
  s->set_brightness(s, -1);      // -2 to 2: Protects highlights in bright clouds
  s->set_saturation(s, 1);       // 0 to 2: Makes the Earth's blue pop
  s->set_contrast(s, 1);         // 0 to 2: Sharpens the look of the horizon
  s->set_sharpness(s, 2);        // Max sharpness to compensate for fixed lenses

  // --- SHUTTER SPEED & GAIN (Anti-Blur) ---
  s->set_gain_ctrl(s, 1);        // Enable Auto Gain
  s->set_agc_gain(s, 2);         // Low gain boost (prevents grainy "noise")
  s->set_exposure_ctrl(s, 1);    // Enable Auto Exposure
  s->set_aec2(s, 0);             // Disable "Night Mode" to keep shutter fast
  s->set_ae_level(s, -1);        // Slightly underexpose to keep the sky dark black

  // --- ORIENTATION ---
  s->set_vflip(s, 0);            // Set to 1 if the camera is mounted upside down
  s->set_hmirror(s, 0);          // Set to 1 if the image is mirrored

  return true;
}

bool initSDCard() {
  // Try Pin 10 first, then Pin 21 if it fails (DFRobot board variance)
  if (SD.begin(10)) return true;
  Serial.println("SD failed on 10, trying 21...");
  if (SD.begin(21)) return true;
  return false;
}

void takePhotoAndSave() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  String path = "/pic_" + String(session_id) + "_" + String(photo_count) + ".jpg";
  File file = SD.open(path.c_str(), FILE_WRITE);
  if (file) {
    file.write(fb->buf, fb->len);
    file.close();
    Serial.println("Saved: " + path);
    photo_count++;
  }
  esp_camera_fb_return(fb);
}

// --- WEB SERVER HANDLERS ---
void handleRoot() {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:sans-serif; background:#f4f4f4; padding:20px;} .stat{font-size:1.5em; margin:10px 0;}</style></head>";
  html += "<body><h1>Flight Recovery</h1>";
  html += "<div class='stat'><b>Session ID:</b> <span id='sid'>" + String(session_id) + "</span></div>";
  html += "<div class='stat'><b>Photo Count:</b> <span id='pcnt'>" + String(photo_count) + "</span></div>";
  html += "<p>Use the Node.js script to download all files.</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleDownload() {
  if (!server.hasArg("file")) { server.send(400, "text/plain", "No File"); return; }
  String path = server.arg("file");
  File file = SD.open(path);
  if (!file || file.isDirectory()) { server.send(404, "text/plain", "Not Found"); return; }
  
  server.streamFile(file, "image/jpeg");
  file.close();
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  
  if (!initSDCard()) {
    Serial.println("SD Card Error!");
    while(1) flashLED(1); 
  }

  if (!initCamera()) {
    Serial.println("Camera Error!");
    flashLED(10);
  }

  if (session_id == 0) session_id = esp_random() % 9000 + 1000;

  // MISSION LOOP
  unsigned long missionDuration = (unsigned long)CAPTURE_DURATION_MIN * 60 * 1000;
  unsigned long startMillis = millis();
  
  Serial.println("Mission Start...");
  while (millis() - startMillis < missionDuration) {
    digitalWrite(LED_PIN, HIGH); // Flash during capture
    takePhotoAndSave();
    digitalWrite(LED_PIN, LOW);
    delay(TIME_TO_SLEEP * 1000);
  }

  // RECOVERY MODE (WiFi + Web Server)
  WiFi.softAP("BalloonCam", "password123");
  server.on("/", handleRoot);
  server.on("/download", handleDownload);
  server.begin();
  
  Serial.print("Recovery Mode Active. Connect to the BalloonCam WiFi then load the following IP in your browser: ");
  Serial.println(WiFi.softAPIP());
  flashLED(5); // Signal that mission is done
}

void loop() {
  server.handleClient();
  delay(5);
}