#include <Arduino.h>
#include <rdm6300.h>
#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ctime>

// Constants and Definitions
#define RDM6300_RX_PIN 4
#define MOSFET 15
#define RGB_STRIP_PIN 5
#define NUM_LEDS 3

// Globals
WebServer server(80);
Preferences preferences;
Rdm6300 rdm6300;
CRGB leds[NUM_LEDS];
const time_t SEVEN_DAYS = 7 * 24 * 60 * 60;
bool isWiFiConfigured = false;

// Function declarations
void setupAccessPoint();
void handleRoot();
void saveCredentials();
void connectToWiFi();
void initializeSPIFFS();
void setupRDM6300();
void saveCardID(String cardID);
bool isCardIDStored(String cardID, time_t &timestamp);
void updateCardTimestamp(String cardID);
void deleteCardID(String cardID);
void CheckCard();
bool checkAccess(String cardID);
void openDoorLock();
void logAccess(String cardID);
void Leds_Blue();
void Leds_Green();
void Leds_Red();
void Leds_Yellow();

void setup() {
  Serial.begin(115200);
  pinMode(MOSFET, OUTPUT);
  digitalWrite(MOSFET, LOW);

  // Initialize FastLED
  FastLED.addLeds<NEOPIXEL, RGB_STRIP_PIN>(leds, NUM_LEDS);
  FastLED.clear();
  FastLED.show();

  preferences.begin("doorlock", false);
  initializeSPIFFS();
  setupRDM6300();

  // Check for Wi-Fi credentials
  if (!preferences.isKey("ssid") || !preferences.isKey("device_id")) {
    setupAccessPoint();
  } else {
    connectToWiFi();
  }

  if (isWiFiConfigured) {
    Serial.println("Starting main functionality...");
    Leds_Yellow();
  } else {
    Serial.println("Failed to configure Wi-Fi. Restarting in AP mode.");
    setupAccessPoint();
  }
}

void setupAccessPoint() {
  WiFi.softAP("ESP32-Setup", "12345678");
  Serial.println("Access Point started. Connect to 'ESP32-Setup' and go to 192.168.4.1");

  server.on("/", handleRoot);
  server.on("/save", saveCredentials);
  server.begin();

  // Keep running the server until credentials are provided
  while (true) {
    server.handleClient();
    delay(10);
  }
}

void handleRoot() {
  String page = "<h1>WiFi and Device ID Setup</h1>"
                "<form action='/save'>"
                "<label>SSID:</label><input name='ssid'><br>"
                "<label>Password:</label><input name='password'><br>"
                "<label>Device ID:</label><input name='device_id'><br>"
                "<input type='submit'></form>";
  server.send(200, "text/html", page);
}

void saveCredentials() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  String device_id = server.arg("device_id");

  if (ssid.length() > 0 && device_id.length() > 0) {
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.putString("device_id", device_id);
    server.send(200, "text/html", "<h1>Saved! Rebooting...</h1>");
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/html", "<h1>Error: SSID and Device ID are required.</h1>");
  }
}

void connectToWiFi() {
  String ssid = preferences.getString("ssid");
  String password = preferences.getString("password");

  WiFi.begin(ssid.c_str(), password.c_str());

  Serial.println("Connecting to Wi-Fi...");
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    isWiFiConfigured = true;
    Serial.println("\nConnected to Wi-Fi.");
    Serial.println("Device ID: " + preferences.getString("device_id"));
  } else {
    isWiFiConfigured = false;
    Serial.println("\nFailed to connect to Wi-Fi.");
  }
}

void initializeSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS. Formatting...");
    while (true) { delay(1000); }
  }
  Serial.println("SPIFFS mounted successfully.");
}

void setupRDM6300() {
  rdm6300.begin(RDM6300_RX_PIN);
}

bool isCardIDStored(String cardID, time_t &timestamp) {
  File file = SPIFFS.open("/cards.txt", FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for reading.");
    return false;
  }

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    int delimiterIndex = line.indexOf(',');
    if (delimiterIndex == -1) continue;

    String storedCardID = line.substring(0, delimiterIndex);
    timestamp = line.substring(delimiterIndex + 1).toInt();

    if (storedCardID == cardID) {
      file.close();
      return true;
    }
  }
  file.close();
  return false;
}

void saveCardID(String cardID) {
  File file = SPIFFS.open("/cards.txt", FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending.");
    return;
  }
  time_t now = time(nullptr);
  file.printf("%s,%ld\n", cardID.c_str(), now);
  file.close();
  Serial.println("Card ID saved to SPIFFS: " + cardID);
}

void deleteCardID(String cardID) {
  File file = SPIFFS.open("/cards.txt", FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for reading.");
    return;
  }

  String tempFileContent = "";

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    int delimiterIndex = line.indexOf(',');
    if (delimiterIndex == -1) continue;

    String storedCardID = line.substring(0, delimiterIndex);

    if (storedCardID != cardID) {
      tempFileContent += line + "\n";
    }
  }
  file.close();

  file = SPIFFS.open("/cards.txt", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing.");
    return;
  }
  file.print(tempFileContent);
  file.close();
  Serial.println("Card ID deleted from SPIFFS: " + cardID);
}

bool checkAccess(String cardID) {
  String device_id = preferences.getString("device_id");
  HTTPClient http;
  String url = "https://lab.cafe/otello/admin/api/check_access/" + device_id + "/" + cardID;

  http.begin(url);
  int httpResponseCode = http.GET();
  if (httpResponseCode == 200) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, http.getString());
    http.end();
    return doc["response"].as<int>() == 1;
  }
  http.end();
  return false;
}

void updateCardTimestamp(String cardID) {
  File file = SPIFFS.open("/cards.txt", FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for reading.");
    return;
  }

  String tempFileContent = "";
  time_t now = time(nullptr);

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    int delimiterIndex = line.indexOf(',');
    if (delimiterIndex == -1) continue;

    String storedCardID = line.substring(0, delimiterIndex);
    time_t timestamp = line.substring(delimiterIndex + 1).toInt();

    if (storedCardID == cardID) {
      tempFileContent += String(storedCardID) + "," + String(now) + "\n";
    } else {
      tempFileContent += line + "\n";
    }
  }
  file.close();

  file = SPIFFS.open("/cards.txt", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing.");
    return;
  }
  file.print(tempFileContent);
  file.close();
  Serial.println("Timestamp updated for card ID: " + cardID);
}

void openDoorLock() {
  digitalWrite(MOSFET, HIGH);
  delay(5000);
  digitalWrite(MOSFET, LOW);
}

void logAccess(String cardID) {
  Serial.println("Logging access...");
  checkAccess(cardID);
}

void CheckCard() {
  if (rdm6300.get_new_tag_id()) {
    int cardCode = rdm6300.get_tag_id();
    String cardID = "01" + String(cardCode, HEX);
    time_t timestamp;

    Serial.print("Detected Card ID: ");
    Serial.println(cardID);

    // Check if the card exists in SPIFFS
    if (isCardIDStored(cardID, timestamp)) {
      // Check if the timestamp is within 7 days
      if (time(nullptr) - timestamp <= SEVEN_DAYS) {
        Serial.println("Card valid in SPIFFS. Granting access.");
        Leds_Green();  // Show green LED for access granted
        openDoorLock();
        logAccess(cardID);  // Log access
      } else {
        Serial.println("Card expired in SPIFFS. Verifying with backend...");
        if (checkAccess(cardID)) {
          Serial.println("Card access verified by backend. Updating timestamp.");
          Leds_Green();  // Show green LED for access granted
          openDoorLock();
          updateCardTimestamp(cardID);  // Update SPIFFS
        } else {
          Serial.println("Access denied. Card removed from SPIFFS.");
          deleteCardID(cardID);  // Remove card from SPIFFS
          Leds_Red();  // Show red LED for access denied
        }
      }
    } else {
      Serial.println("Card not found in SPIFFS. Verifying with backend...");
      if (checkAccess(cardID)) {
        Serial.println("Card access granted by backend. Saving to SPIFFS.");
        Leds_Green();  // Show green LED for access granted
        openDoorLock();
        saveCardID(cardID);  // Save new card to SPIFFS
      } else {
        Serial.println("Access denied.");
        Leds_Red();  // Show red LED for access denied
      }
    }
  } else {
    // Waiting for card
    Leds_Blue();  // Indicate waiting state
  }
}


void Leds_Blue() { 
  fill_solid(leds, NUM_LEDS, CRGB::Blue); 
  FastLED.show(); 
}

void Leds_Green() { 
  fill_solid(leds, NUM_LEDS, CRGB::Green); 
  FastLED.show(); 
}

void Leds_Red() { 
  fill_solid(leds, NUM_LEDS, CRGB::Red); 
  FastLED.show(); 
}

void Leds_Yellow() { 
  fill_solid(leds, NUM_LEDS, CRGB::Yellow); 
  FastLED.show(); 
}

void loop() {
  if (isWiFiConfigured) {
    CheckCard();
  } else {
    server.handleClient();
  }
}
