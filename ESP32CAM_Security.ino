/*
 * ESP32-CAM Security System with AI-Thinker ESP32-CAM
 * Features:
 * - PIR Motion Detection
 * - Photo Capture with Timestamp (RTC)
 * - Storage on SPIFFS (Internal Flash)
 * - Email Notification with Photo Attachment
 */

#include "esp_camera.h"
#include "SPIFFS.h"
#include "WiFi.h"
#include "time.h"
#include "esp_mail_client.h"

// ==================== PIN DEFINITIONS ====================
#define PIR_SENSOR_PIN    13  // GPIO13 - PIR Motion Sensor
#define RTC_SDA_PIN       14  // GPIO14 - RTC I2C SDA
#define RTC_SCL_PIN       15  // GPIO15 - RTC I2C SCL
#define LED_FLASH         4   // GPIO4 - LED Flash

// ==================== CAMERA PINS (AI-Thinker ESP32-CAM) ====================
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

// ==================== WiFi & Email Configuration ====================
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465
#define SENDER_EMAIL "your_email@gmail.com"
#define SENDER_PASSWORD "your_app_password"  // Gmail App Password
#define RECIPIENT_EMAIL "recipient@example.com"

// ==================== EMAIL CLIENT ====================
SMTPSession smtp;
ESP_Mail_Session session;

// ==================== GLOBAL VARIABLES ====================
bool motionDetected = false;
unsigned long lastCaptureTime = 0;
const unsigned long CAPTURE_INTERVAL = 10000; // 10 seconds between captures

// Simple RTC Variables (using system time synchronized via NTP)
struct tm timeinfo;
time_t now;

// ==================== FUNCTION PROTOTYPES ====================
void initCamera();
void initPIRSensor();
void initSPIFS();
void initWiFi();
void configureRTC();
void captureAndSavePhoto();
void sendEmailWithPhoto(const char* imagePath);
void getFormattedDateTime(char* buffer);
String getEpochTimestamp();

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\nESP32-CAM Security System Starting...");
  
  // Initialize LED
  pinMode(LED_FLASH, OUTPUT);
  digitalWrite(LED_FLASH, LOW);
  
  // Initialize SPIFFS for file storage
  initSPIFS();
  
  // Initialize PIR Sensor
  initPIRSensor();
  
  // Initialize WiFi
  initWiFi();
  
  // Initialize Camera
  initCamera();
  
  // Configure RTC (time synchronization)
  configureRTC();
  
  Serial.println("System initialization complete!");
}

// ==================== MAIN LOOP ====================
void loop() {
  // Check PIR Sensor
  if (digitalRead(PIR_SENSOR_PIN) == HIGH) {
    if (!motionDetected) {
      motionDetected = true;
      Serial.println("Motion Detected!");
      
      if (millis() - lastCaptureTime >= CAPTURE_INTERVAL) {
        captureAndSavePhoto();
        lastCaptureTime = millis();
      }
    }
  } else {
    motionDetected = false;
  }
  
  delay(100);
}

// ==================== CAMERA INITIALIZATION ====================
void initCamera() {
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
  config.pin_sda = SIOD_GPIO_NUM;
  config.pin_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;  // 640x480
  config.jpeg_quality = 10;
  config.fb_count = 1;
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  
  sensor_t * s = esp_camera_sensor_get();
  s->set_brightness(s, 0);
  s->set_contrast(s, 0);
  s->set_saturation(s, 0);
  s->set_special_effect(s, 0);
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 1);
  s->set_wb_mode(s, 0);
  s->set_expose_ctrl(s, 1);
  s->set_aec_value(s, 300);
  s->set_gain_ctrl(s, 1);
  s->set_agc_gain(s, 0);
  s->set_gainceiling(s, (gainceiling_t)0);
  s->set_bpc(s, 0);
  s->set_wpc(s, 1);
  s->set_raw_gma(s, 1);
  s->set_lenc(s, 1);
  s->set_hmirror(s, 0);
  s->set_vflip(s, 0);
  
  Serial.println("Camera initialized successfully!");
}

// ==================== PIR SENSOR INITIALIZATION ====================
void initPIRSensor() {
  pinMode(PIR_SENSOR_PIN, INPUT);
  Serial.println("PIR Sensor initialized on GPIO 13");
}

// ==================== SPIFFS INITIALIZATION ====================
void initSPIFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  Serial.println("SPIFFS mounted successfully");
}

// ==================== WiFi INITIALIZATION ====================
void initWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed!");
  }
}

// ==================== RTC CONFIGURATION ====================
void configureRTC() {
  // Synchronize time with NTP server
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  Serial.println("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  int attempts = 0;
  while (now < 24 * 3600 && attempts < 20) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    attempts++;
  }
  
  Serial.println();
  localtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.println(asctime(&timeinfo));
}

// ==================== CAPTURE AND SAVE PHOTO ====================
void captureAndSavePhoto() {
  digitalWrite(LED_FLASH, HIGH);  // Turn on flash
  delay(100);
  
  camera_fb_t * fb = esp_camera_fb_get();
  
  if (!fb) {
    Serial.println("Camera capture failed");
    digitalWrite(LED_FLASH, LOW);
    return;
  }
  
  // Get current time
  time_t now = time(nullptr);
  localtime_r(&now, &timeinfo);
  
  // Create filename with timestamp
  char filename[64];
  strftime(filename, sizeof(filename), "/IMG_%Y%m%d_%H%M%S.jpg", &timeinfo);
  
  // Save to SPIFFS
  File file = SPIFFS.open(filename, FILE_WRITE);
  
  if (!file) {
    Serial.println("Failed to open file for writing");
    esp_camera_fb_return(fb);
    digitalWrite(LED_FLASH, LOW);
    return;
  }
  
  file.write(fb->buf, fb->len);
  file.close();
  
  Serial.printf("Photo saved: %s (Size: %d bytes)\n", filename, fb->len);
  
  esp_camera_fb_return(fb);
  digitalWrite(LED_FLASH, LOW);
  
  // Send email with photo
  sendEmailWithPhoto(filename);
}

// ==================== GET FORMATTED DATE & TIME ====================
void getFormattedDateTime(char* buffer) {
  time_t now = time(nullptr);
  localtime_r(&now, &timeinfo);
  strftime(buffer, 64, "%d/%m/%Y %H:%M:%S", &timeinfo);
}

// ==================== GET EPOCH TIMESTAMP ====================
String getEpochTimestamp() {
  time_t now = time(nullptr);
  localtime_r(&now, &timeinfo);
  
  char timestamp[20];
  strftime(timestamp, sizeof(timestamp), "%d%m%Y%H%M%S", &timeinfo);
  return String(timestamp);
}

// ==================== SEND EMAIL WITH PHOTO ====================
void sendEmailWithPhoto(const char* imagePath) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. Cannot send email.");
    return;
  }
  
  Serial.println("Preparing to send email...");
  
  // Configure session
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = SENDER_EMAIL;
  session.login.password = SENDER_PASSWORD;
  session.login.user_domain = "";
  
  // Declare the object for sending Email
  SMTP_Message message;
  
  message.sender.name = "ESP32-CAM Security";
  message.sender.email = SENDER_EMAIL;
  message.subject = "Motion Detected - Security Alert";
  message.addRecipient("User", RECIPIENT_EMAIL);
  
  // Prepare message body
  char dateTime[64];
  getFormattedDateTime(dateTime);
  
  String htmlMsg = "<html><body>";
  htmlMsg += "<h2>Motion Detection Alert</h2>";
  htmlMsg += "<p><strong>Date & Time:</strong> ";
  htmlMsg += dateTime;
  htmlMsg += "</p>";
  htmlMsg += "<p><strong>Location:</strong> Main Entrance</p>";
  htmlMsg += "<p>A motion has been detected by the ESP32-CAM security system.</p>";
  htmlMsg += "<p>Please see the attached photo for details.</p>";
  htmlMsg += "</body></html>";
  
  message.html.content = htmlMsg.c_str();
  message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  
  // Add photo attachment
  SMTP_Attachment attachment;
  attachment.descr.filename = imagePath;
  attachment.descr.mime = "image/jpeg";
  attachment.file.path = imagePath;
  attachment.file.storage_type = esp_mail_file_storage_type_spiffs;
  message.addAttachment(attachment);
  
  // Connect to server and send Email
  if (!smtp.connect(&session)) {
    Serial.printf("SMTP connection error: %s\n", smtp.errorReason().c_str());
    return;
  }
  
  if (!MailClient.sendEmail(&smtp, &message)) {
    Serial.printf("Error sending Email: %s\n", smtp.errorReason().c_str());
  } else {
    Serial.println("Email sent successfully!");
  }
  
  // Disconnect
  smtp.closeSession();
}

// ==================== UTILITY: List Files in SPIFFS ====================
void listSPIFFSFiles() {
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  
  Serial.println("Files in SPIFFS:");
  while (file) {
    Serial.printf("  %s (%d bytes)\n", file.name(), file.size());
    file = root.openNextFile();
  }
}
