#include <Arduino.h>
#include <rdm6300.h>
#include <FastLED.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Ping.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ctime>

WebServer server(80);
Preferences preferences;

// Constants and Definitions
#define RDM6300_RX_PIN 4
#define MOSFET 15
#define RGB_STRIP_PIN 5
#define NUM_LEDS 3

const time_t SEVEN_DAYS = 7 * 24 * 60 * 60;

// Global Variables
Rdm6300 rdm6300;
CRGB leds[NUM_LEDS];
WiFiMulti wifiMulti; // Create a WiFiMulti object

// Function Declarations
void setupAccessPoint();
void connectToWiFi();
void setupWiFi();
void setupLEDs();
void setupRDM6300();
void RGBAnimation();
bool checkAccess(String cardID);
void openDoorLock();
void CheckCard();
void Leds_Blue();
void Leds_Green();
void Leds_Red();
void Leds_Yellow();
void pingGoogle();
void initializeSPIFFS();
void saveCardID(String cardID);
bool isCardIDStored(String cardID, time_t &timestamp);
void updateCardTimestamp(String cardID);
void deleteCardID(String cardID);
void logAccess(String cardID);

int door_timer = 5000;

void setup() {
  Serial.begin(115200);
  pinMode(MOSFET, OUTPUT);
  digitalWrite(MOSFET, LOW);

  preferences.begin("doorlock", false);
  initializeSPIFFS();

  if (!preferences.isKey("ssid")) {
    setupAccessPoint();
  } else {
    connectToWiFi();
  }

  Leds_Yellow();
  setupRDM6300();

  wifiMulti.addAP("Lab.systems", "createavity");
  wifiMulti.addAP("Mullet_Bar-staff", "sexyhairstyle");
  setupWiFi();

  Serial.println("\nReady");
}

void setupAccessPoint() {
  WiFi.softAP("ESP32-DoorLock", "createavity");
  server.begin();
}

void connectToWiFi() {
  String ssid = preferences.getString("ssid");
  String password = preferences.getString("password");
  WiFi.begin(ssid.c_str(), password.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting to WiFi..");
  }
  Serial.println("Connected to the WiFi network");
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
  FastLED.addLeds<NEOPIXEL, RGB_STRIP_PIN>(leds, NUM_LEDS);
  fill_solid(leds, NUM_LEDS, CRGB::Yellow);
  FastLED.show();
}

void setupWiFi() {
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting to WiFi..");
  }
  Serial.println("Connected to the WiFi network");
}

void pingGoogle() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return;
  }
  bool success = Ping.ping("www.google.com", 3);
  if (!success) {
    Serial.println("Ping failed");
    return;
  }
  Serial.println("Ping successful. I am ready.");
}

void setupRDM6300() {
  rdm6300.begin(RDM6300_RX_PIN);
}

bool checkAccess(String cardID) {
    HTTPClient http;
    String url = "https://lab.cafe/otello/admin/api/check_access/2247/" + cardID;

    Serial.print("API Request for checkAccess: ");
    Serial.println(url);

    http.begin(url);

    int httpResponseCode = http.GET();
    String response = http.getString();

    Serial.print("HTTP Response Code: ");
    Serial.println(httpResponseCode);
    Serial.println("API Response:");
    Serial.println(response);

    if (httpResponseCode == 200) {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, response);
        return doc["response"].as<int>() == 1; // Check if "response" is 1
    } else {
        Serial.println("Failed to get access. Response code: " + String(httpResponseCode));
    }

    http.end();
    return false;
}

void openDoorLock() {
  digitalWrite(MOSFET, HIGH);
  delay(door_timer);
  digitalWrite(MOSFET, LOW);
}

void initializeSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS. Formatting...");
    while (true) { delay(1000); }
  }
  Serial.println("SPIFFS mounted successfully.");
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

void logAccess(String cardID) {
  Serial.println("Logging access to backend...");
  checkAccess(cardID); // Call the API to log the access
}

void CheckCard() {
  if (rdm6300.get_new_tag_id()) {
    int cardCode = rdm6300.get_tag_id();
    String cardID = "01" + String(cardCode, HEX);
    time_t timestamp;

    Serial.print("Card ID: ");
    Serial.println(cardID);

    if (isCardIDStored(cardID, timestamp)) {
      time_t now = time(nullptr);
      if (now - timestamp <= SEVEN_DAYS) {
        Serial.println("Card ID found in SPIFFS, access granted.");
        Leds_Green();
        openDoorLock();
        logAccess(cardID); // Log access to backend after unlocking
        return;
      } else {
        Serial.println("Card ID expired in SPIFFS, verifying with backend...");
        if (checkAccess(cardID)) {
          Leds_Green();
          openDoorLock();
          updateCardTimestamp(cardID);
          return;
        } else {
          Serial.println("Access denied. Card ID no longer valid. Deleting from SPIFFS.");
          deleteCardID(cardID);
          Leds_Red();
          delay(1000);
          return;
        }
      }
    }

    Serial.println("Card ID not found in SPIFFS, querying backend...");
    if (checkAccess(cardID)) {
      Leds_Green();
      openDoorLock();
      saveCardID(cardID);
    } else {
      Leds_Red();
      delay(1000);
    }
  }
  delay(10);
}

void loop() {
  server.handleClient();
  Leds_Blue();
  CheckCard();
}
