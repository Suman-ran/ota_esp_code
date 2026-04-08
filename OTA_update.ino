/*
  ESP32 - OLED Display + OTA Update via GitHub

  How it works:
    - ESP32 checks version.json on your GitHub repo every 30 seconds
    - If a newer version is found, downloads firmware.bin from GitHub and flashes
    - Works from ANY network, completely free

  Hardware:
    - ESP32 (any variant)
    - SH110X OLED 128x64 via I2C
        SDA -> GPIO 21
        SCL -> GPIO 22
        VCC -> 3.3V
        GND -> GND

  Libraries required (install via Arduino Library Manager):
    - Adafruit SH110X
    - Adafruit GFX Library
    - ArduinoJson
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <ArduinoJson.h>

// ---------- WiFi credentials ----------
const char* WIFI_SSID     = "camserverv31";
const char* WIFI_PASSWORD = "qweasd1234";

// ---------- Firmware version (bump this before each release) ----------
#define FW_VERSION "1.0.0"

// ---------- GitHub raw URLs ----------
// Replace YOUR_USERNAME and YOUR_REPO with your actual GitHub details
// Example: https://raw.githubusercontent.com/john/esp32-ota/main/version.json
#define GITHUB_USER    "Suman-ran"
#define GITHUB_REPO    "ota_esp_code"
#define GITHUB_BRANCH  "main"

#define RAW_BASE    "https://raw.githubusercontent.com/" GITHUB_USER "/" GITHUB_REPO "/" GITHUB_BRANCH
#define VERSION_URL  RAW_BASE "/version.json"
#define FIRMWARE_URL RAW_BASE "/firmware.bin"

// ---------- How often to check for updates ----------
#define OTA_CHECK_INTERVAL  30000   // 30 seconds

// ---------- OLED settings ----------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define OLED_ADDRESS  0x3C

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------- Display helper ----------
void showText(const char* line1,
              const char* line2 = "",
              const char* line3 = "",
              const char* line4 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);  display.println(line1);
  display.setCursor(0, 16); display.println(line2);
  display.setCursor(0, 32); display.println(line3);
  display.setCursor(0, 48); display.println(line4);
  display.display();
}

// ---------- WiFi ----------
void connectWiFi() {
  showText("Connecting WiFi...", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
    char dots[5] = "";
    for (int i = 0; i < (attempts % 4); i++) strcat(dots, ".");
    showText("Connecting WiFi...", WIFI_SSID, dots);
  }

  if (WiFi.status() == WL_CONNECTED) {
    showText("WiFi Connected!", WIFI_SSID, WiFi.localIP().toString().c_str());
    Serial.println("IP: " + WiFi.localIP().toString());
    delay(2000);
  } else {
    showText("WiFi FAILED!", "Restarting...");
    delay(3000);
    ESP.restart();
  }
}

// ---------- OTA via GitHub ----------
void checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) return;

  showText("Checking update...", "GitHub...");
  Serial.println("Checking GitHub for update...");

  // Use secure client — GitHub requires HTTPS
  WiFiClientSecure client;
  client.setInsecure(); // skip certificate check (simpler; fine for OTA)

  HTTPClient http;
  http.begin(client, VERSION_URL);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.printf("Version check failed: HTTP %d\n", httpCode);
    showText("Check failed", ("HTTP: " + String(httpCode)).c_str());
    http.end();
    delay(2000);
    return;
  }

  // Parse {"version":"1.0.1"}
  StaticJsonDocument<64> doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();

  if (err) {
    Serial.println("JSON parse error");
    showText("JSON error!");
    delay(2000);
    return;
  }

  const char* serverVersion = doc["version"];
  Serial.printf("Device: v%s  |  GitHub: v%s\n", FW_VERSION, serverVersion);

  if (strcmp(serverVersion, FW_VERSION) == 0) {
    showText("Up to date!", ("v" + String(FW_VERSION)).c_str());
    Serial.println("Firmware is up to date.");
    delay(2000);
    return;
  }

  // New version found — flash it
  showText("Update found!",
           ("GitHub: v" + String(serverVersion)).c_str(),
           ("Device: v"  + String(FW_VERSION)).c_str(),
           "Downloading...");
  Serial.printf("Updating to v%s\n", serverVersion);
  delay(1000);

  // Progress bar on OLED
  httpUpdate.onProgress([](int current, int total) {
    if (total == 0) return;
    int percent = (current * 100) / total;
    char pct[20];
    snprintf(pct, sizeof(pct), "Progress: %d%%", percent);

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);  display.println("OTA Updating...");
    display.setCursor(0, 16); display.println(pct);
    int barWidth = (SCREEN_WIDTH * percent) / 100;
    display.drawRect(0, 40, SCREEN_WIDTH, 12, SH110X_WHITE);
    display.fillRect(0, 40, barWidth, 12, SH110X_WHITE);
    display.display();
  });

  // Use a fresh secure client for the firmware download
  WiFiClientSecure dlClient;
  dlClient.setInsecure();

  t_httpUpdate_return result = httpUpdate.update(dlClient, FIRMWARE_URL);

  switch (result) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("Update failed: %s\n", httpUpdate.getLastErrorString().c_str());
      showText("Update FAILED!", httpUpdate.getLastErrorString().c_str());
      delay(3000);
      break;

    case HTTP_UPDATE_NO_UPDATES:
      showText("No update needed.");
      delay(2000);
      break;

    case HTTP_UPDATE_OK:
      showText("Update OK!", "Rebooting...");
      // ESP32 reboots automatically
      break;
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  if (!display.begin(OLED_ADDRESS, true)) {
    Serial.println("SH110X not found!");
    while (true);
  }

  display.clearDisplay();
  display.display();

  // Splash screen
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(10, 10);
  display.println("ESP32");
  display.setTextSize(1);
  display.setCursor(10, 36);
  display.println("OTA + OLED Demo");
  display.setCursor(10, 50);
  display.print("FW: v"); display.println(FW_VERSION);
  display.display();
  delay(2000);

  connectWiFi();
  checkForUpdate();  // check on every boot

  showText("Ready!",
           ("IP: " + WiFi.localIP().toString()).c_str(),
           "Checks every 30s",
           ("FW: v" + String(FW_VERSION)).c_str());
}

// ---------- Loop ----------
void loop() {
  static unsigned long lastCheck   = 0;
  static unsigned long lastDisplay = 0;

  if (millis() - lastCheck >= OTA_CHECK_INTERVAL) {
    lastCheck = millis();
    checkForUpdate();
  }

  if (millis() - lastDisplay >= 5000) {
    lastDisplay = millis();

    unsigned long secs  = millis() / 1000;
    unsigned long mins  = secs / 60;
    unsigned long hours = mins / 60;
    secs %= 60; mins %= 60;

    char uptime[20];
    snprintf(uptime, sizeof(uptime), "Up: %02luh%02lum%02lus", hours, mins, secs);

    showText("ESP32 Running",
             ("IP: " + WiFi.localIP().toString()).c_str(),
             uptime,
             ("FW: v" + String(FW_VERSION)).c_str());
  }
}
