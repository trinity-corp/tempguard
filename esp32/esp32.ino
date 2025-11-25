/*
  TempGuard firmware
  Device: ESP32
  Description: Reads humidity sensor data (AM2320), processes values, 
               and sends them to the server or cloud service.
  Author: Andriy Tymchuk
  Repository: https://github.com/trinity-corp/TempGuard/
*/

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Adafruit_Sensor.h" // AM2320 Library
#include "Adafruit_AM2320.h" // AM2320 Library

// Defining screen settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C // I2C address used by the display
#define OLED_SDA 21    // Display's SDA pin number
#define OLED_SCL 22    // Display's SCL pin number

// Intializing display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Initializing NVS
Preferences prefs;

// Initializing webserver
WebServer server(80);
HTTPClient http;

// DS18S20 Initializng
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

const char* ap_ssid = "TempGuard";     // Access point's SSID
const char* ap_password = "12345678"; // Access point's password

// Initializating variables
String sta_ssid = "";
String sta_password = "";
String device_id = "";
String connectionStatus = "";
String api_base_url = "https://0.0.0.0/api"; // API endpoint URL used by default
String reading_time = "15"; // Default reading interval

bool screenEnabled = true;
bool isConfigured = false;
unsigned long lastReading = 0;
unsigned long readingInterval;
unsigned long previousMillis = 0;
const unsigned long displayReadingInterval = 5000;

// Initializing functions
void checkConfiguration();
void startAPMode();
void setupWebServer();
void connectToWiFi();
void clearConfiguration();
void saveConfiguration();
float readSensor();
void displayData(float final_value);
void displayMessage(String line1 = "", String line2 = "", String line3 = "", String line4 = "");
void sendDataToAPI(float final_value);
void handleApiCommand(String command, String payload);
String generateDeviceID();

void setup() {
  Serial.begin(115200);
  prefs.begin("config", false);
  tempSensor.begin();

  // Enable OLED screen using defined OLED_SDA and OLED_SCL adressess
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 allocation failed")); 
    for(;;); // If screen does not starts device will not work
  }

  // Get the device_id
  device_id = prefs.getString("device_id");
  if (device_id.length() == 0) {
    device_id = generateDeviceID(); // If device_id empty generate a new one 
    prefs.putString("device_id", device_id); // Write new device_id to NVS
  }

  // Get the API URL
  String api_base_url = prefs.getString("api_base_url", api_base_url);

  // Define reading time
  reading_time = prefs.getString("reading_time", "15");
  int reading_minutes = reading_time.toInt(); // Convert string reading_time to integer
  if (reading_minutes <= 0 || reading_minutes > 1440) {
    reading_minutes = 15;
    reading_time = "15";
    prefs.putString("reading_time", reading_time);
    Serial.println("Invalid reading time, set to default: 15 minutes");
  }
  readingInterval = (unsigned long)reading_minutes * 60UL * 1000UL;

  Serial.println("Reading interval: " + String(reading_minutes) + " minutes = " + String(readingInterval) + " ms");

  displayMessage("TempGuard", "Device ID: " + device_id, "Initializing...");
  delay(1000);

  // Checking configuration
  checkConfiguration();
  if (isConfigured) {
    displayMessage("Connecting to", "saved WiFi...", sta_ssid);
    connectToWiFi();
  } else {
    displayMessage("Configuration", "Mode", "Connect to TempGuard AP");
    startAPMode();
    setupWebServer();
  }

  // Connection status is unknown before readings sent
  connectionStatus = "Unknown";

  Serial.println("TempGuard Started!");
  Serial.println("Device ID: " + device_id);
  Serial.println("Reading interval: " + String(readingInterval/1000) + " seconds");

  // Uncomment for verbose mode
  // Serial.println("=== Verbose data ===");
  // Serial.println("device_id: " + prefs.getString("device_id"));
  // Serial.println("sta_ssid: " + prefs.getString("sta_ssid"));
  // Serial.println("sta_password: " +prefs.getString("sta_password"));
  // Serial.println("isConfigured: " + prefs.getBool("isConfigured"));
  // Serial.println("Reading interval: " + prefs.getString("reading_time"));
  // Serial.println("API URL: " + prefs.getString("api_base_url"));

}

void loop() {
  server.handleClient();
  isConfigured = prefs.getBool("isConfigured");
  
  // If device not configured (Was not setted up or factory reset)
  if (!isConfigured) {
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 2000) {
      lastDisplayUpdate = millis();
      displayMessage("TempGuard AP Mode", "SSID: " + String(ap_ssid), "Password: " + String(ap_password), "URL: " + WiFi.softAPIP().toString());
    }
    return;
  }

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= displayReadingInterval) {
    previousMillis = currentMillis;

    float final_value = readSensor();

    Serial.print("Humidity: "); Serial.print(final_value,1);
    Serial.println("% RH"); //

    displayData(final_value, connectionStatus);
  }
    
  if (lastReading == 0 || millis() - lastReading >= readingInterval) {
    lastReading = millis();

    float final_value = readSensor();
    sendDataToAPI(final_value);
    
    Serial.println("=== Sensor readings sent ===");
  }
}

// Function for reading your sensor readings
float readSensor() {
  tempSensor.requestTemperatures();
  float final_value = tempSensor.getTempCByIndex(0);
  return final_value;
  }
}

// Function for displaying data on the OLED screen
void displayData(float final_value, String connectionStatus) {
  if (!screenEnabled) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  
  if (WiFi.getMode() == WIFI_MODE_AP) {
    display.println("TempGuard - AP Mode");
  } else {
    display.print(device_id);
    display.print(" - ");
    display.println(connectionStatus);
  }
  display.drawLine(0, 12, 128, 12, WHITE);

  display.setCursor(0,20);
  display.setTextSize(2);
  display.print(final_value, 2);
  display.setTextSize(1);
  display.println("% RH");

  display.display();
}

void displayMessage(String line1, String line2, String line3, String line4) {
  if (!screenEnabled) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println(line1);
  display.setCursor(0,10);
  display.println(line2);
  display.setCursor(0,20);
  display.println(line3);
  display.setCursor(0,30);
  display.println(line4);
  display.display();
}


void sendDataToAPI(float final_value) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Cannot send data - WiFi not connected");
    connectionStatus = "Offline"; // Connection status is offline when can not connect to the API but WiFi
    return;
  }

  String url = api_base_url + "/sensor-readings/"; // Device will send to API page /sensor-readings/
  Serial.println("Sending to: " + url);

  DynamicJsonDocument doc(256);
  doc["device_id"] = device_id;
  doc["final_value"] = final_value;

  String payload;
  serializeJson(doc, payload);
  Serial.println("Payload: " + payload);

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);

  Serial.println("Sending POST request...");
  int httpCode = http.POST(payload);
  
  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("HTTP Code: " + String(httpCode) + ", Response: " + response);
    connectionStatus = "Online";
    
    if (response.length() > 2) {
      DynamicJsonDocument resDoc(256);
      DeserializationError error = deserializeJson(resDoc, response);
      
      if (!error && resDoc.containsKey("command")) {
        // If command found in response
        String command = resDoc["command"];
        String payload = resDoc["payload"] | "";
        Serial.println("Found command in response: " + command);
        handleApiCommand(command, payload);
      }
    }
  } else {
    Serial.println("HTTP POST failed: " + http.errorToString(httpCode));
    connectionStatus = "Offline"; // Connection status changes to offline when API not available
  }

  http.end();
}

// Functions for handling commands being in HTTP responses payloads sent by API
void handleApiCommand(String command, String payload) {
  Serial.println("Executing command: " + command + " | Payload: " + payload);
  if (command == "disable_screen") {
    // Disable screen command
    screenEnabled = false; // Mark screen as disabled
    display.ssd1306_command(SSD1306_DISPLAYOFF); // Disable screen using Adafruit library
    Serial.println("Screen disabled by command");
  }
  else if (command == "enable_screen") {
    // Enable screen command
    screenEnabled = true; // Mark screen as enabled
    display.ssd1306_command(SSD1306_DISPLAYON); // Enable screen using Adafruit library
    Serial.println("Screen enabled by command");
  }
  else if (command == "reboot") {
    // Reboot ESP command
    displayMessage("Rebooting...", "", "", "");
    delay(2000);
    ESP.restart(); // Restart ESP
  }
  else if (command == "change_reading_time") {
    // This command updates the reading interval using the value provided in the payload
    prefs.putString("reading_time", payload); // Save new interval to NVS
    displayMessage("Reading time", "changed to " + payload + "m", "Restarting...", "");
    delay(2000);
    ESP.restart(); // Restart ESP
  }
  else if (command == "factory_reset") {
    // Function for clearing the ESP NVS
    clearConfiguration();
    displayMessage("Factory Reset", "Restarting...", "", "");
    delay(2000);
    ESP.restart(); // Restart ESP
  }
  else {
    Serial.println("Unknown command: " + command);
  }
}

/* 
  Function for generating device ID using MAC address
  The generated ID's will always be the same and unique* 
*/ 
String generateDeviceID() {
  String id = "HG-"; // 'HG' stands for TempGuard
  id += String((uint32_t)ESP.getEfuseMac(), HEX); // *This function gets only the last 32 bits of MAC address, so in very rare situations the ID's may repeat
  id.toUpperCase();
  return id;
}

// Function for checking configuration
void checkConfiguration() {
  isConfigured = prefs.getBool("isConfigured");
  if (isConfigured == true) {
    sta_ssid = prefs.getString("sta_ssid");
    sta_password = prefs.getString("sta_password");
    reading_time = prefs.getString("reading_time");

    if (sta_ssid.length() > 0) {
      prefs.putBool("isConfigured", true);
      Serial.println("Device is configured");
    } else {
      prefs.putBool("isConfigured", false);
    }
  } else {
    prefs.putBool("isConfigured", false);
  }
}

// Function for starting the access point mode
void startAPMode() {
  Serial.println("Starting AP mode for configuration...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("AP SSID: ");
  Serial.println(ap_ssid);
}

// Function for setting up the website
void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    String html = R"=====(
    <!DOCTYPE html>
    <html>
    <head>
      <title>TempGuard WiFi Configuration</title>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <style>
        body { font-family: Arial, sans-serif; margin: 40px; background-color: #f0f0f0; }
        .container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
        h2 { color: #2c3e50; text-align: center; }
        input[type="text"], input[type="password"] { 
          width: 100%; padding: 12px; margin: 8px 0; 
          border: 1px solid #ccc; border-radius: 4px; 
          box-sizing: border-box;
        }
        input[type="submit"] { 
          width: 100%; background-color: #3498db; 
          color: white; padding: 14px; border: none; 
          border-radius: 4px; cursor: pointer; 
          font-size: 16px; margin-top: 10px;
        }
        input[type="submit"]:hover { background-color: #2980b9; }
        label { font-weight: bold; color: #34495e; }
        .device-info { background: #f8f9fa; padding: 15px; border-radius: 5px; margin-bottom: 15px; }
      </style>
    </head>
    <body>
      <div class="container">
        <h2>TempGuard WiFi Setup</h2>
        
        <div class="device-info">
          <strong>Device ID:</strong> )=====" + device_id + R"=====(<br>
          <strong>API URL:</strong> )=====" + api_base_url + R"=====(<br>
        </div>
        
        <form action="/configure" method="post">
          <label for="ssid">WiFi Network Name:</label>
          <input type="text" id="ssid" name="ssid" required placeholder="Enter your WiFi name">
          
          <label for="password">WiFi Password:</label>
          <input type="password" id="password" name="password" placeholder="Enter your WiFi password">
          
          <input type="submit" value="Save & Connect">
        </form>
      </div>
    </body>
    </html>
    )=====";
    server.send(200, "text/html", html);
  });

  // After pressing 'Save & Connect' button
  server.on("/configure", HTTP_POST, []() {
    if (server.hasArg("ssid")) {
      sta_ssid = server.arg("ssid");
      sta_password = server.arg("password");
      
      saveConfiguration();
      
      // Send success response
      String html = R"=====(
      <!DOCTYPE html>
      <html>
      <head>
        <title>Configuration Saved</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <style>
          body { font-family: Arial, sans-serif; margin: 40px; text-align: center; background-color: #f0f0f0; }
          .success { color: #27ae60; font-size: 24px; font-weight: bold; }
          .container { max-width: 400px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
        </style>
      </head>
      <body>
        <div class="container">
          <div class="success">Configuration Saved!</div>
          <p>Device will restart and connect in 5 seconds...</p>
        </div>
        <script>
          setTimeout(function() {
            window.location.href = "/";
          }, 5000);
        </script>
      </body>
      </html>
      )=====";
      
      server.send(200, "text/html", html);
      delay(5000);
      ESP.restart();
    } else {
      server.send(400, "text/plain", "Error: Missing WiFi Name");
    }
  });

  server.begin();
  Serial.println("HTTP server started");
}

// Function for saving configuration to NVS
void saveConfiguration() {  
  prefs.putString("sta_ssid", sta_ssid);
  prefs.putString("sta_password", sta_password);
  prefs.putBool("isConfigured", true);
  Serial.println("Configuration saved to NVS");
}

// Function for connecting to a WiFi
void connectToWiFi() {  
  sta_ssid = prefs.getString("sta_ssid", sta_ssid);
  sta_password = prefs.getString("sta_password", sta_password);
  Serial.println("Connecting to saved WiFi...");
  Serial.println("SSID: " + sta_ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(sta_ssid.c_str(), sta_password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    displayMessage("WiFi Connected!", "IP: " + WiFi.localIP().toString(), "Reading sensor...", "");
    delay(2000);

    lastReading = 0;
    
  } else {
    Serial.println("\nFailed to connect to WiFi. Starting AP mode...");
    displayMessage("WiFi Connection", "Failed!", "Starting AP mode...", "");
    delay(2000);
    startAPMode();
    setupWebServer();
  }
}

void clearConfiguration() {
  prefs.clear();
  prefs.putBool("isConfigured", false);
  Serial.println("Configuration cleared");
}
