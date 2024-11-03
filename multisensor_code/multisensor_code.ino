#include <WiFiManager.h>
#include <EEPROM.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Preferences.h>

AsyncWebServer server(80);
#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME280 bme; // I2C

// EEPROM addresses and constants
const int EEPROM_SIZE = 512;
const int SSID_ADDR = 0;
const int PASS_ADDR = 100;
const int FLAG_ADDR = 200; // Address for the Wi-Fi flag

// Variables for sensor data
float temperature = 24.5;
float pressure = 1013.25;
float humidity = 45.5;

// RGB light color
int red = 0, green = 0, blue = 0;

// IR code parameters
String irProtocol = "";
String irCode = "";

// Define interrupt pin
const int interruptPin = 27; // Pin 27 for the interrupt
volatile bool interruptTriggered = false; // Flag to indicate interrupt occurred

void IRAM_ATTR handleInterrupt() {
  interruptTriggered = true; // Set flag to indicate interrupt was triggered
}

// Function to handle interrupt action
void onInterruptTriggered() {
  Serial.println("Interrupt triggered on pin 27!");
  // Perform any additional action here
  // e.g., toggle an LED or update a variable
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);

  // Set up interrupt pin
  pinMode(interruptPin, INPUT_PULLUP); // Configure as input with pull-up resistor
  attachInterrupt(digitalPinToInterrupt(interruptPin), handleInterrupt, RISING); // Trigger on HIGH

  // Configure BME280 sensor
  if (!bme.begin(0x76)) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
  }

  // Initialize WiFi manager
  WiFiManager wifiManager;

  // Check if credentials are stored
  if (EEPROM.read(FLAG_ADDR) != 1) {
    // Start the captive portal if no credentials are stored
    wifiManager.setAPCallback(configModeCallback);
    if (!wifiManager.autoConnect("MultiSensor setup")) {
      Serial.println("Failed to connect and hit timeout");
      delay(3000);
      ESP.restart();
    }
    // Save new credentials to EEPROM
    writeStringToEEPROM(SSID_ADDR, WiFi.SSID());
    writeStringToEEPROM(PASS_ADDR, WiFi.psk());
    EEPROM.write(FLAG_ADDR, 1);  // Set the flag to indicate credentials are saved
    EEPROM.commit();
  } else {
    // Connect with stored credentials
    String storedSSID = readStringFromEEPROM(SSID_ADDR);
    String storedPass = readStringFromEEPROM(PASS_ADDR);
    WiFi.begin(storedSSID.c_str(), storedPass.c_str());
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
  }

  Serial.println(WiFi.localIP());

  // Define routes
  server.on("/temp", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", get_temp());
  });

  server.on("/pressure", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", get_press());
  });

  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", get_hum());
  });
  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request){
    reset_sensor();
    request->send(200, "application/json", get_hum());
  });

  server.on("/light", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("r", true) && request->hasParam("g", true) && request->hasParam("b", true)) {
      red = request->getParam("r", true)->value().toInt();
      green = request->getParam("g", true)->value().toInt();
      blue = request->getParam("b", true)->value().toInt();
      Serial.printf("Set RGB to (%d, %d, %d)\n", red, green, blue);
      request->send(200, "text/plain", "RGB updated");
    } else {
      request->send(400, "text/plain", "Missing parameters");
    }
  });

  server.on("/ir", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("protocol", true) && request->hasParam("code", true)) {
      irProtocol = request->getParam("protocol", true)->value();
      irCode = request->getParam("code", true)->value();
      Serial.printf("IR command received: Protocol %s, Code %s\n", irProtocol.c_str(), irCode.c_str());
      request->send(200, "text/plain", "IR command received");
    } else {
      request->send(400, "text/plain", "Missing parameters");
    }
  });

  server.begin();
  Serial.println("Server started!");
}

void loop() {
  // Check if the interrupt was triggered
  if (interruptTriggered) {
    interruptTriggered = false; // Reset the flag
    onInterruptTriggered(); // Call the function to handle the interrupt event
  }

  delay(5000); // Adjust delay as needed
}

// Callback when entering configuration mode
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode. Please connect to the AP to set credentials.");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

String get_temp(){
  return String(bme.readTemperature());
}

String get_hum(){
  return String(bme.readHumidity());
}

String get_press(){
  return String(bme.readPressure() / 100.0F);
}
void reset_sensor(){
  WiFiManager wifiManager;
  wifiManager.resetSettings();  // Clears any WiFi settings saved by WiFiManager
  Serial.println("WiFiManager settings reset.");

  // Step 2: Disconnect from WiFi and clear credentials from NVS
  WiFi.disconnect(true, true); // Disconnect and erase WiFi credentials from NVS
  Serial.println("WiFi.disconnect() called with erase flag set.");

  // Step 3: Completely clear NVS using Preferences
  Preferences preferences;
  preferences.begin("nvs", false);
  preferences.clear();         // Clear all data from NVS
  preferences.end();
  Serial.println("NVS completely cleared.");

  // Step 4: Initialize and clear the entire EEPROM
  EEPROM.begin(512);           // Adjust size based on your usage
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i, 0);        // Set each byte to 0
  }
  EEPROM.commit();
  Serial.println("EEPROM completely cleared.");

  // Optional: Verification (print first 100 EEPROM bytes to Serial)
  Serial.print("EEPROM data: ");
  for (int i = 0; i < 100; i++) {
    Serial.print(EEPROM.read(i));
    Serial.print(" ");
  }
  Serial.println();
  EEPROM.end();

  Serial.println("All credentials and settings have been cleared.");
}
// EEPROM functions for reading/writing strings
String readStringFromEEPROM(int addr) {
  String data;
  for (int i = addr; i < addr + 100; i++) {
    byte readByte = EEPROM.read(i);
    if (readByte == 0) break;
    data += char(readByte);
  }
  return data;
}

void writeStringToEEPROM(int addr, String data) {
  for (int i = 0; i < 100; i++) {
    if (i < data.length()) {
      EEPROM.write(addr + i, data[i]);
    } else {
      EEPROM.write(addr + i, 0);
    }
  }
}
