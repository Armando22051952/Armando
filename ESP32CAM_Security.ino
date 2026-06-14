/*
 * ESP32-CAM Security System with AI-Thinker ESP32-CAM
 * Features:
 * - PIR Motion Detection
 * - Photo Capture with Timestamp (RTC via NTP)
 * - Storage on SPIFFS (Internal Flash)
 * - Email Notification with Photo Attachment (SMTP)
 * 
 * Libraries Required (Built-in Arduino):
 * - WiFi.h
 * - SPIFFS.h
 * - time.h
 * - esp_camera.h
 * - WiFiClientSecure.h
 */

#include "esp_camera.h"
#include "SPIFFS.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include "time.h"
#include <string.h>
#include <stdlib.h>

// ==================== PIN DEFINITIONS ====================
#define PIR_SENSOR_PIN    13  // GPIO13 - PIR Motion Sensor
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
const char* smtp_host = "smtp.gmail.com";
const int smtp_port = 465;
const char* sender_email = "your_email@gmail.com";
const char* sender_password = "your_app_password";  // Gmail App Password
const char* recipient_email = "recipient@example.com";

// ==================== GLOBAL VARIABLES ====================
bool motionDetected = false;
unsigned long lastCaptureTime = 0;
const unsigned long CAPTURE_INTERVAL = 10000; // 10 seconds between captures
struct tm timeinfo;

// ==================== BASE64 ENCODING ====================
const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64Encode(uint8_t* data, size_t len) {
  String result = "";
  int i = 0;
  
  while (i < len) {
    uint8_t b1 = data[i++];
    uint8_t b2 = (i < len) ? data[i++] : 0;
    uint8_t b3 = (i < len) ? data[i++] : 0;
    
    uint32_t combined = (b1 << 16) | (b2 << 8) | b3;
    
    result += base64_table[(combined >> 18) & 0x3F];
    result += base64_table[(combined >> 12) & 0x3F];
    result += (i - 1 < len) ? base64_table[(combined >> 6) & 0x3F] : '=';
    result += (i < len) ? base64_table[combined & 0x3F] : '=';
  }
  
  return result;
}

// ==================== SMTP CLIENT (NATIVE IMPLEMENTATION) ====================
class SimpleSMTPClient {
  private:
    WiFiClientSecure client;
    String response;
    
  public:
    bool connect(const char* host, int port) {
      Serial.printf("Connecting to SMTP server: %s:%d\n", host, port);
      return client.connect(host, port);
    }
    
    String readResponse() {
      response = "";
      while (client.available()) {
        char c = client.read();
        response += c;
      }
      Serial.print(response);
      return response;
    }
    
    void sendCommand(const String& cmd) {
      Serial.printf(">>> %s\n", cmd.c_str());
      client.println(cmd);
      delay(100);
      readResponse();
    }
    
    bool authenticate(const char* email, const char* password) {
      // Send EHLO
      sendCommand("EHLO esp32");
      
      // Start TLS
      sendCommand("STARTTLS");
      
      // Upgrade connection to TLS
      if (!client.startSSL()) {
        Serial.println("Failed to upgrade to TLS");
        return false;
      }
      
      delay(500);
      readResponse();
      
      // Send EHLO again after TLS
      sendCommand("EHLO esp32");
      
      // Authenticate with base64 encoded credentials
      sendCommand("AUTH LOGIN");
      
      String encodedEmail = base64Encode((uint8_t*)email, strlen(email));
      sendCommand(encodedEmail);
      
      String encodedPassword = base64Encode((uint8_t*)password, strlen(password));
      sendCommand(encodedPassword);
      
      return true;
    }
    
    void sendMail(const char* from, const char* to, const char* subject, 
                  const char* bodyText, const char* filename, uint8_t* imageData, size_t imageSize) {
      
      // MAIL FROM
      String mailFrom = "MAIL FROM:<";
      mailFrom += from;
      mailFrom += ">";
      sendCommand(mailFrom);
      
      // RCPT TO
      String rcptTo = "RCPT TO:<";
      rcptTo += to;
      rcptTo += ">";
      sendCommand(rcptTo);
      
      // DATA
      sendCommand("DATA");
      
      // Prepare email headers and body
      String headers = "From: ";
      headers += from;
      headers += "\r\nTo: ";
      headers += to;
      headers += "\r\nSubject: ";
      headers += subject;
      headers += "\r\nMIME-Version: 1.0\r\n";
      headers += "Content-Type: multipart/mixed; boundary=\"boundary123\"\r\n\r\n";
      
      client.print(headers);
      
      // Text part
      client.print("--boundary123\r\n");
      client.print("Content-Type: text/html; charset=\"UTF-8\"\r\n");
      client.print("Content-Transfer-Encoding: 7bit\r\n\r\n");
      client.print(bodyText);
      client.print("\r\n\r\n");
      
      // Image attachment
      client.print("--boundary123\r\n");
      client.print("Content-Type: image/jpeg\r\n");
      client.print("Content-Transfer-Encoding: base64\r\n");
      client.printf("Content-Disposition: attachment; filename=\"%s\"\r\n\r\n", filename);
      
      String encodedImage = base64Encode(imageData, imageSize);
      
      // Send base64 image in chunks
      const int chunkSize = 76;
      for (int i = 0; i < encodedImage.length(); i += chunkSize) {
        client.println(encodedImage.substring(i, i + chunkSize));
      }
      
      client.print("\r\n--boundary123--\r\n");
      
      // End of DATA
      sendCommand(".");
      
      delay(500);
      readResponse();
    }
    
    void quit() {
      sendCommand("QUIT");
      client.stop();
    }
    
    void close() {
      if (client.connected()) {
        client.stop();
      }
    }
};

SimpleSMTPClient smtpClient;

// ==================== FUNCTION PROTOTYPES ====================
void initCamera();
void initPIRSensor();
void initSPIFS();
void initWiFi();
void configureRTC();
void captureAndSavePhoto();
void sendEmailWithPhoto(const char* imagePath);
void getFormattedDateTime(char* buffer);

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

// ==================== GET FORMATTED DATE & TIME ====================
void getFormattedDateTime(char* buffer) {
  time_t now = time(nullptr);
  localtime_r(&now, &timeinfo);
  strftime(buffer, 64, "%d/%m/%Y %H:%M:%S", &timeinfo);
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
  
  // Store image data for email
  uint8_t* imageData = fb->buf;
  size_t imageSize = fb->len;
  
  esp_camera_fb_return(fb);
  digitalWrite(LED_FLASH, LOW);
  
  // Send email with photo
  sendEmailWithPhoto(filename);
}

// ==================== SEND EMAIL WITH PHOTO ====================
void sendEmailWithPhoto(const char* imagePath) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. Cannot send email.");
    return;
  }
  
  Serial.println("Preparing to send email...");
  
  // Read image from SPIFFS
  File imageFile = SPIFFS.open(imagePath, FILE_READ);
  if (!imageFile) {
    Serial.println("Failed to open image file");
    return;
  }
  
  size_t imageSize = imageFile.size();
  uint8_t* imageData = (uint8_t*)malloc(imageSize);
  
  if (!imageData) {
    Serial.println("Failed to allocate memory for image");
    imageFile.close();
    return;
  }
  
  imageFile.read(imageData, imageSize);
  imageFile.close();
  
  // Disable SSL verification (not recommended for production)
  smtpClient.client.setInsecure();
  
  // Connect to SMTP server
  if (!smtpClient.connect(smtp_host, smtp_port)) {
    Serial.println("Failed to connect to SMTP server");
    free(imageData);
    return;
  }
  
  delay(500);
  smtpClient.readResponse();
  
  // Authenticate
  if (!smtpClient.authenticate(sender_email, sender_password)) {
    Serial.println("SMTP authentication failed");
    smtpClient.close();
    free(imageData);
    return;
  }
  
  // Prepare email body
  char dateTime[64];
  getFormattedDateTime(dateTime);
  
  String htmlBody = "<html><body>";
  htmlBody += "<h2>Motion Detection Alert</h2>";
  htmlBody += "<p><strong>Date &amp; Time:</strong> ";
  htmlBody += dateTime;
  htmlBody += "</p>";
  htmlBody += "<p><strong>Location:</strong> Main Entrance</p>";
  htmlBody += "<p>A motion has been detected by the ESP32-CAM security system.</p>";
  htmlBody += "<p>Please see the attached photo for details.</p>";
  htmlBody += "</body></html>";
  
  // Send email
  smtpClient.sendMail(sender_email, recipient_email, "Motion Detected - Security Alert",
                      htmlBody.c_str(), imagePath, imageData, imageSize);
  
  // Cleanup
  smtpClient.quit();
  free(imageData);
  
  Serial.println("Email sending completed!");
}
