#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ── WiFi Credentials ──────────────────────────────────────────────────────────
const char* WIFI_SSID = "Mayra";
const char* WIFI_PASS = "plilinli";

// ── PIN DEFINITIONS ───────────────────────────────────────────────────────────
const int PIN_RED[3]  = {15, 21, 39};
const int PIN_BLUE[3] = {2, 23, 27};

const int CH_RED[3]   = {2, 4, 6};   // LEDC channels for Red
const int CH_BLUE[3]  = {1, 3, 5};   // LEDC channels for Blue

const int PUMP_PIN    = 5;
const int DHT_PIN     = 33;
const int DS18B20_PIN = 25;
const int TEMT_PIN[4] = {35, 34, 39, 36};  // Update pins when wired separately

// ── CONFIG ────────────────────────────────────────────────────────────────────
int   floorQty          = 1;      // Active floors (1–3)
int   redIntensity[3]   = {0, 0, 0};
int   blueIntensity[3]  = {0, 0, 0};
bool  pumpState         = false;
long  scheduleMinutes   = 0;      // 0 = no schedule
unsigned long scheduleStart = 0;

// ── SENSOR VALUES ─────────────────────────────────────────────────────────────
float humidity          = 0.0;
float ambientTemp       = 0.0;
float waterTemp         = 0.0;
int   lightIntensity[4] = {0, 0, 0, 0};

// ── LIBRARY INSTANCES ─────────────────────────────────────────────────────────
OneWire           oneWire(DS18B20_PIN);
DallasTemperature tempSensors(&oneWire);
DHT               dht(DHT_PIN, DHT22);
AsyncWebServer    server(80);

// ── FUNCTION PROTOTYPES ───────────────────────────────────────────────────────
void     readSensors();
void     writeLEDs();
void     setPump(bool on);
void     setupWiFi();
void     setupFilesystem();
void     setupRoutes();
void     checkPumpSchedule();
String   buildSensorJSON();

// ─────────────────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n[AeroGarden] Booting...");

  // LED pins
  for (int i = 0; i < floorQty; i++) {
    pinMode(PIN_RED[i],  OUTPUT);
    pinMode(PIN_BLUE[i], OUTPUT);
    ledcSetup(CH_RED[i],  1000, 8);   // 5 kHz, 8-bit resolution
    ledcSetup(CH_BLUE[i], 1000, 8);
    ledcAttachPin(PIN_RED[i],  CH_RED[i]);
    ledcAttachPin(PIN_BLUE[i], CH_BLUE[i]);
  }

  // Pump & sensors
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);

  for (int i = 0; i < 4; i++) {
    pinMode(TEMT_PIN[i], INPUT);
  }
  pinMode(waterTemp, INPUT);
  dht.begin();
  tempSensors.begin();

  setupWiFi();
  setupFilesystem();
  setupRoutes();

  server.begin();
  Serial.println("[AeroGarden] Web server started.");
}

// ─────────────────────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  static unsigned long lastRead = 0;
  if (millis() - lastRead >= 2000) {
    readSensors();
    lastRead = millis();
  }
  checkPumpSchedule();
}

// ─────────────────────────────────────────────────────────────────────────────
//  WiFi
// ─────────────────────────────────────────────────────────────────────────────
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] Connecting");
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
    delay(500);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] Failed – starting AP mode.");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("AeroGarden-Setup", "aerogarden");
    Serial.println("[WiFi] AP: AeroGarden-Setup | IP: " + WiFi.softAPIP().toString());
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Filesystem (LittleFS)
// ─────────────────────────────────────────────────────────────────────────────
void setupFilesystem() {
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS mount failed!");
    return;
  }
  Serial.println("[FS] LittleFS mounted.");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Web Server Routes
// ─────────────────────────────────────────────────────────────────────────────
void setupRoutes() {

  // ── Static files ──
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // ── GET /api/sensors → JSON snapshot ──
  server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", buildSensorJSON());
  });

  // ── POST handlers — parsed via onRequestBody (no extra library needed) ──
  //
  // onRequestBody fires after the full body is received, giving us the raw
  // bytes. We parse JSON there and respond immediately.

  server.onRequestBody([](AsyncWebServerRequest* req,
                          uint8_t* data, size_t len,
                          size_t index, size_t total) {

    // Only handle when the full body has arrived
    if (index + len != total) return;

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
      req->send(400, "application/json", "{\"error\":\"Bad JSON\"}");
      return;
    }

    String url = req->url();

    // ── /api/leds → { red: 0-255, blue: 0-255 } ──
    if (url == "/api/leds" && req->method() == HTTP_POST) {
      if (doc.containsKey("red") && doc.containsKey("blue")) {
        int red  = constrain((int)doc["red"],  0, 255);
        int blue = constrain((int)doc["blue"], 0, 255);
        for (int i = 0; i < floorQty; i++) {
          redIntensity[i]  = red;
          blueIntensity[i] = blue;
        }
        writeLEDs();
        req->send(200, "application/json", "{\"ok\":true}");
      } else {
        req->send(400, "application/json", "{\"error\":\"Missing fields\"}");
      }
    }

    // ── /api/pump → { state: 0|1 } ──
    else if (url == "/api/pump" && req->method() == HTTP_POST) {
      if (doc.containsKey("state")) {
        setPump((int)doc["state"] == 1);
        req->send(200, "application/json", "{\"ok\":true}");
      } else {
        req->send(400, "application/json", "{\"error\":\"Missing state\"}");
      }
    }

    // ── /api/pump/schedule → { minutes: N } ──
    else if (url == "/api/pump/schedule" && req->method() == HTTP_POST) {
      if (doc.containsKey("minutes")) {
        scheduleMinutes = (int)doc["minutes"];
        scheduleStart   = millis();
        req->send(200, "application/json", "{\"ok\":true}");
      } else {
        req->send(400, "application/json", "{\"error\":\"Missing minutes\"}");
      }
    }

    // ── /api/settings → { floors: N } ──
    else if (url == "/api/settings" && req->method() == HTTP_POST) {
      if (doc.containsKey("floors")) {
        floorQty = constrain((int)doc["floors"], 1, 3);
      }
      req->send(200, "application/json", "{\"ok\":true}");
    }
  });

  // ── 404 fallback ──
  server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "Not found");
  });
}

// ─────────────────────────────────────────────────────────────────────────────
//  Build JSON sensor payload
// ─────────────────────────────────────────────────────────────────────────────
String buildSensorJSON() {
  StaticJsonDocument<512> doc;
  doc["humidity"]    = isnan(humidity)    ? 0 : roundf(humidity * 10) / 10.0f;
  doc["ambientTemp"] = isnan(ambientTemp) ? 0 : roundf(ambientTemp * 10) / 10.0f;
  doc["waterTemp"]   = waterTemp < -50    ? 0 : roundf(waterTemp * 10) / 10.0f;
  doc["pump"]        = pumpState ? 1 : 0;
  doc["uptime"]      = (unsigned long)(millis() / 1000UL);
  doc["ip"]          = (WiFi.status() == WL_CONNECTED)
                          ? WiFi.localIP().toString()
                          : WiFi.softAPIP().toString();

  JsonArray light = doc.createNestedArray("light");
  for (int i = 0; i < 4; i++) light.add(lightIntensity[i]);

  String out;
  serializeJson(doc, out);
  return out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Read Sensors
// ─────────────────────────────────────────────────────────────────────────────
void readSensors() {
  humidity    = dht.readHumidity();
  ambientTemp = dht.readTemperature();

  tempSensors.requestTemperatures();
  waterTemp = tempSensors.getTempCByIndex(0);

  for (int i = 0; i < 4; i++) {
    lightIntensity[i] = analogRead(TEMT_PIN[i]);
  }

  // Debug output
  Serial.printf("[Sensors] H:%.1f%% T:%.1f°C W:%.1f°C L:%d,%d,%d,%d\n",
    humidity, ambientTemp, waterTemp,
    lightIntensity[0], lightIntensity[1], lightIntensity[2], lightIntensity[3]);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Write LEDs via LEDC PWM
// ─────────────────────────────────────────────────────────────────────────────
void writeLEDs() {
  for (int i = 0; i < floorQty; i++) {
    ledcWrite(CH_RED[i],  redIntensity[i]);
    ledcWrite(CH_BLUE[i], blueIntensity[i]);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pump control
// ─────────────────────────────────────────────────────────────────────────────
void setPump(bool on) {
  pumpState = on;
  digitalWrite(PUMP_PIN, on ? HIGH : LOW);
  Serial.printf("[Pump] %s\n", on ? "ON" : "OFF");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pump schedule check (runs in loop)
// ─────────────────────────────────────────────────────────────────────────────
void checkPumpSchedule() {
  if (scheduleMinutes <= 0) return;
  unsigned long elapsed = (millis() - scheduleStart) / 60000UL; // minutes
  if (elapsed >= (unsigned long)scheduleMinutes) {
    Serial.println("[Pump] Schedule elapsed – turning ON");
    setPump(true);
    scheduleMinutes = 0;
  }
}
