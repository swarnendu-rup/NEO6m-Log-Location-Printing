#include <HardwareSerial.h>
#include <TinyGPS++.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Telegram Bot settings (optional - for sending alerts)
String bot_token = "YOUR_BOT_TOKEN";
String chat_id = "YOUR_CHAT_ID";

// GPS and sensor variables
TinyGPSPlus gps;
HardwareSerial gps_serial(2); // Use UART2 for GPS
double LATITUDE, LONGITUDE, SPEED;
String latitude = "", longitude = "";
String alertMessage = "";

// Pin definitions
#define TILT_SENSOR_PIN 34    // Analog pin for tilt sensor
#define LED_PIN 2             // Built-in LED
#define GPS_RX_PIN 16         // GPS module TX connects here
#define GPS_TX_PIN 17         // GPS module RX connects here

// Timing variables
unsigned long lastAlert = 0;
const unsigned long alertCooldown = 30000; // 30 seconds between alerts

void setup() {
  Serial.begin(115200);
  gps_serial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  
  // Pin setup
  pinMode(TILT_SENSOR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("ESP32 GPS Tilt Security System Starting...");
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  Serial.println("System ready. Monitoring for tilt...");
}

String formatCoordinate(double coordinate, int precision = 6) {
  String result = "";
  
  if (coordinate < 0) {
    result = "-";
    coordinate = -coordinate;
  }
  
  int wholePart = (int)coordinate;
  double fractionalPart = coordinate - wholePart;
  
  result += String(wholePart);
  result += ".";
  
  for (int i = 0; i < precision; i++) {
    fractionalPart *= 10;
    int digit = (int)fractionalPart;
    result += String(digit);
    fractionalPart -= digit;
  }
  
  return result;
}

void sendTelegramAlert(String message) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + bot_token + "/sendMessage";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    DynamicJsonDocument doc(1024);
    doc["chat_id"] = chat_id;
    doc["text"] = message;
    doc["parse_mode"] = "HTML";
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpResponseCode = http.POST(jsonString);
    
    if (httpResponseCode > 0) {
      Serial.println("Alert sent successfully!");
    } else {
      Serial.println("Failed to send alert");
    }
    
    http.end();
  }
}

void sendWebhookAlert(String message) {
  // Alternative: Send to your own server/webhook
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String webhookURL = "https://your-server.com/webhook"; // Replace with your webhook
    
    http.begin(webhookURL);
    http.addHeader("Content-Type", "application/json");
    
    DynamicJsonDocument doc(1024);
    doc["alert"] = "Tilt Sensor Activated";
    doc["latitude"] = LATITUDE;
    doc["longitude"] = LONGITUDE;
    doc["speed"] = SPEED;
    doc["message"] = message;
    doc["timestamp"] = millis();
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpResponseCode = http.POST(jsonString);
    
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("Webhook response: " + response);
    }
    
    http.end();
  }
}

void createAlertMessage() {
  alertMessage = "🚨 SECURITY ALERT 🚨\n\n";
  alertMessage += "Tilt sensor activated!\n\n";
  
  if (gps.location.isValid()) {
    alertMessage += "📍 Location Details:\n";
    alertMessage += "Latitude: " + latitude + "\n";
    alertMessage += "Longitude: " + longitude + "\n";
    alertMessage += "Speed: " + String(SPEED, 2) + " km/h\n\n";
    
    alertMessage += "🗺️ View on Map:\n";
    alertMessage += "https://www.google.com/maps/search/?api=1&query=";
    alertMessage += latitude + "," + longitude + "\n\n";
    
    alertMessage += "Time: " + String(millis()/1000) + " seconds since boot";
  } else {
    alertMessage += "⚠️ GPS location not available\n";
    alertMessage += "Time: " + String(millis()/1000) + " seconds since boot";
  }
}

void loop() {
  // Read GPS data
  while (gps_serial.available() > 0) {
    if (gps.encode(gps_serial.read())) {
      if (gps.location.isUpdated()) {
        LATITUDE = gps.location.lat();
        LONGITUDE = gps.location.lng();
        latitude = formatCoordinate(LATITUDE);
        longitude = formatCoordinate(LONGITUDE);
        
        if (gps.speed.isValid()) {
          SPEED = gps.speed.kmph();
        }
      }
    }
  }
  
  // Check for GPS timeout
  if (millis() > 30000 && gps.charsProcessed() < 10) {
    Serial.println("No GPS detected: check wiring.");
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
    return;
  }
  
  // Read tilt sensor
  int tiltReading = analogRead(TILT_SENSOR_PIN);
  
  // Check if tilt threshold exceeded and cooldown period passed
  if (tiltReading > 2000 && (millis() - lastAlert > alertCooldown)) {
    Serial.println("TILT DETECTED! Reading: " + String(tiltReading));
    
    // Turn on LED
    digitalWrite(LED_PIN, HIGH);
    
    // Update GPS data if valid
    if (gps.location.isValid()) {
      LATITUDE = gps.location.lat();
      LONGITUDE = gps.location.lng();
      latitude = formatCoordinate(LATITUDE);
      longitude = formatCoordinate(LONGITUDE);
      SPEED = gps.speed.kmph();
      
      Serial.println("GPS Data:");
      Serial.println("Lat: " + latitude);
      Serial.println("Lng: " + longitude);
      Serial.println("Speed: " + String(SPEED) + " km/h");
    }
    
    // Create and send alert
    createAlertMessage();
    Serial.println("\n" + alertMessage);
    
    // Send via Telegram (uncomment if using)
    // sendTelegramAlert(alertMessage);
    
    // Send via webhook (uncomment if using)
    // sendWebhookAlert(alertMessage);
    
    // Update last alert time
    lastAlert = millis();
    
    // Keep LED on for 2 seconds
    delay(2000);
    digitalWrite(LED_PIN, LOW);
  }
  
  // Small delay to prevent excessive readings
  delay(100);
}