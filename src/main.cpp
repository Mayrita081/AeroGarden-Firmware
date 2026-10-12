#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_NeoPixel.h>
#include "Control.h"

// Existing prototype wiring. Assign distinct ADC pins when more sensors exist.
constexpr int NEO_PIN = 0, PUMP_PIN = 1, DHT_PIN = 7, DS18B20_PIN = 8;
constexpr int TEMT_PIN[] = {3, 3, 3, 3};
constexpr int LEDS_PER_FLOOR = 32, MAX_FLOORS = 3;
Adafruit_NeoPixel strip(LEDS_PER_FLOOR * MAX_FLOORS, NEO_PIN, NEO_GRB + NEO_KHZ800);
OneWire oneWire(DS18B20_PIN);
DallasTemperature tempSensors(&oneWire);
DHT dht(DHT_PIN, DHT11);
AsyncWebServer server(80);
Preferences prefs;
SemaphoreHandle_t stateMutex;
struct StateLock {
  StateLock() { xSemaphoreTake(stateMutex, portMAX_DELAY); }
  ~StateLock() { xSemaphoreGive(stateMutex); }
};
PumpControl pump;
int activeFloors = 1, redIntensity = 0, blueIntensity = 0;
bool autoLight = false;
int lightTarget = 50;
float lightScale = 0;
int actualRed = 0, actualBlue = 0;
float humidity = NAN, ambientTemp = NAN, waterTemp = NAN;
int lightIntensity[4] = {};
String wifiSSID, wifiPassword, wifiStatus = "Not configured";
String wifiMode = "setup", wifiError;
bool wifiPending = false, wifiAttempting = false;
uint32_t wifiStart = 0, wifiApplyAt = 0;

void writeLEDs() {
  auto color = scaledColor(redIntensity, blueIntensity, autoLight, lightScale);
  actualRed = color.red; actualBlue = color.blue;
  for (int px = 0; px < strip.numPixels(); ++px)
    strip.setPixelColor(px, px < activeFloors * LEDS_PER_FLOOR ? strip.Color(actualRed, 0, actualBlue) : 0);
  strip.show();
}
void writePump() { digitalWrite(PUMP_PIN, pump.on ? HIGH : LOW); }
void saveControls() {
  prefs.putInt("floors", activeFloors);
  prefs.putInt("red", redIntensity); prefs.putInt("blue", blueIntensity);
  prefs.putBool("autoLight", autoLight); prefs.putInt("target", lightTarget);
  prefs.putBool("cycle", pump.mode == PumpMode::Cycle);
  prefs.putUInt("onMins", pump.onMinutes); prefs.putUInt("offMins", pump.offMinutes);
}
String snapshot() {
  StaticJsonDocument<1536> doc;
  doc["humidity"] = humidity; doc["ambientTemp"] = ambientTemp; doc["waterTemp"] = waterTemp;
  auto light = doc.createNestedArray("light");
  for (int value : lightIntensity) light.add(value);
  doc["pump"] = pump.on ? 1 : 0;
  doc["pumpMode"] = pump.mode == PumpMode::Cycle ? "cycle" : pump.mode == PumpMode::Timer ? "timer" : "manual";
  doc["remainingSeconds"] = pump.remaining(millis()) / 1000;
  doc["onMinutes"] = pump.onMinutes; doc["offMinutes"] = pump.offMinutes;
  doc["red"] = redIntensity; doc["blue"] = blueIntensity;
  doc["actualRed"] = actualRed; doc["actualBlue"] = actualBlue;
  doc["autoLight"] = autoLight; doc["lightTarget"] = lightTarget; doc["floors"] = activeFloors;
  doc["uptime"] = millis() / 1000;
  doc["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  doc["wifiStatus"] = wifiStatus; doc["ssid"] = wifiSSID;
  doc["wifiMode"] = wifiMode; doc["wifiError"] = wifiError;
  String out; serializeJson(doc, out); return out;
}
bool integer(JsonVariant v, int low, int high) { return v.is<int>() && v.as<int>() >= low && v.as<int>() <= high; }
void bad(AsyncWebServerRequest* req, const char* message) {
  StaticJsonDocument<192> doc; doc["error"] = message;
  String out; serializeJson(doc, out); req->send(400, "application/json", out);
}
void ok(AsyncWebServerRequest* req) { req->send(200, "application/json", snapshot()); }
void post(const char* path, ArJsonRequestHandlerFunction callback) {
  auto handler = new AsyncCallbackJsonWebHandler(path, [callback, path](AsyncWebServerRequest* req, JsonVariant& json) {
    if (req->url() != path) { req->send(404); return; }
    if (!json.is<JsonObject>()) { bad(req, "Expected a JSON object"); return; }
    StateLock lock;
    if (req->url() != "/api/wifi" && wifiMode != "ready") {
      req->send(409, "application/json", "{\"error\":\"Complete Wi-Fi setup first\"}"); return;
    }
    callback(req, json);
  });
  handler->setMethod(HTTP_POST); handler->setMaxContentLength(1024); server.addHandler(handler);
}
void setupRoutes() {
  server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest* req) {
    StateLock lock;
    auto response = req->beginResponse(200, "application/json", snapshot());
    response->addHeader("Cache-Control", "no-store"); req->send(response);
  });
  post("/api/leds", [](AsyncWebServerRequest* req, JsonVariant& j) {
    if (!integer(j["red"], 0, 255) || !integer(j["blue"], 0, 255) || !j["autoLight"].is<bool>() || !integer(j["lightTarget"], 1, 100)) {
      bad(req, "Set red/blue to 0-255 and light target to 1-100"); return;
    }
    if (j["autoLight"].as<bool>() && j["red"].as<int>() + j["blue"].as<int>() == 0) {
      bad(req, "Choose a nonzero color mix for automatic light"); return;
    }
    redIntensity = j["red"]; blueIntensity = j["blue"];
    autoLight = j["autoLight"]; lightTarget = j["lightTarget"];
    writeLEDs(); saveControls(); ok(req);
  });
  // Register the specific schedule route before its parent.
  post("/api/pump/schedule", [](AsyncWebServerRequest* req, JsonVariant& j) {
    String mode = j["mode"] | "";
    if (mode == "manual") pump.manual(pump.on);
    else if (mode == "timer" && integer(j["minutes"], 1, 10080)) pump.timer(j["minutes"], millis());
    else if (mode == "cycle" && integer(j["onMinutes"], 1, 10080) && integer(j["offMinutes"], 1, 10080))
      pump.cycle(j["onMinutes"], j["offMinutes"], millis());
    else { bad(req, "Use timer or cycle with durations of 1-10080 minutes"); return; }
    writePump(); saveControls(); ok(req);
  });
  post("/api/pump", [](AsyncWebServerRequest* req, JsonVariant& j) {
    if (!integer(j["state"], 0, 1)) { bad(req, "State must be 0 or 1"); return; }
    pump.manual(j["state"].as<int>() == 1); writePump(); saveControls(); ok(req);
  });
  post("/api/settings", [](AsyncWebServerRequest* req, JsonVariant& j) {
    if (!integer(j["floors"], 1, MAX_FLOORS)) { bad(req, "Floors must be 1-3"); return; }
    activeFloors = j["floors"]; writeLEDs(); saveControls(); ok(req);
  });
  post("/api/wifi", [](AsyncWebServerRequest* req, JsonVariant& j) {
    if (wifiMode == "connecting") {
      req->send(409, "application/json", "{\"error\":\"Connection attempt already in progress\"}"); return;
    }
    if (!j["ssid"].is<const char*>() || !j["password"].is<const char*>()) { bad(req, "SSID and password required"); return; }
    String ssid = j["ssid"], password = j["password"];
    if (ssid.length() < 1 || ssid.length() > 32 || (password.length() != 0 && (password.length() < 8 || password.length() > 63))) {
      bad(req, "SSID: 1-32 bytes; password: 8-63 bytes, or empty for an open network"); return;
    }
    // Persist before connecting; failed attempts return to a clean setup flow.
    prefs.remove("ssid");
    prefs.putString("password", password); prefs.putString("ssid", ssid);
    if (prefs.getString("ssid", "") != ssid || prefs.getString("password", "") != password) {
      prefs.remove("ssid"); prefs.remove("password");
      req->send(500, "application/json", "{\"error\":\"Unable to save credentials. Please retry.\"}"); return;
    }
    wifiMode = "connecting"; wifiError = "";
    wifiSSID = ssid; wifiPassword = password;
    wifiPending = true; wifiApplyAt = millis(); wifiStatus = "Connecting"; ok(req);
  });
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");
  server.onNotFound([](AsyncWebServerRequest* req) { req->send(404, "application/json", "{\"error\":\"Not found\"}"); });
}
void beginConnection() {
  wifiMode = "connecting"; wifiError = "";
  WiFi.disconnect(false); WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  wifiStart = millis(); wifiAttempting = true; wifiPending = false; wifiStatus = "Connecting";
}
void setup() {
  Serial.begin(115200); stateMutex = xSemaphoreCreateMutex();
  pinMode(PUMP_PIN, OUTPUT); digitalWrite(PUMP_PIN, LOW);
  strip.begin(); strip.clear(); strip.show();
  prefs.begin("aerogarden", false);
  activeFloors = constrain(prefs.getInt("floors", 1), 1, MAX_FLOORS);
  redIntensity = constrain(prefs.getInt("red", 0), 0, 255); blueIntensity = constrain(prefs.getInt("blue", 0), 0, 255);
  autoLight = prefs.getBool("autoLight", false); lightTarget = constrain(prefs.getInt("target", 50), 1, 100);
  if (prefs.getBool("cycle", false))
    pump.cycle(constrain(prefs.getUInt("onMins", 5), 1U, 10080U), constrain(prefs.getUInt("offMins", 5), 1U, 10080U), millis());
  // Boot starts a full OFF interval for repeating schedules; one-shot timers reset.
  writeLEDs();
  dht.begin();
  tempSensors.begin();
  tempSensors.setWaitForConversion(false);
  tempSensors.requestTemperatures();
  for (int pin : TEMT_PIN) pinMode(pin, INPUT);
  WiFi.persistent(false); WiFi.setHostname("aerogarden"); WiFi.mode(WIFI_AP_STA); WiFi.setSleep(false);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP("AeroGarden-Setup", "aerogarden");
  wifiSSID = prefs.getString("ssid", ""); wifiPassword = prefs.getString("password", "");
  if (wifiSSID.length()) beginConnection();
  if (MDNS.begin("aerogarden")) MDNS.addService("http", "tcp", 80);
  if (!LittleFS.begin(false)) Serial.println("LittleFS missing: upload filesystem image.");
  setupRoutes();
  server.begin();
  Serial.println("AeroGarden: http://aerogarden.local or AeroGarden-Setup at http://192.168.4.1");
}
void loop() {
  uint32_t now = millis();
  {
    StateLock lock;
    now = millis();
    if (pump.tick(now)) writePump();
    if (wifiPending && uint32_t(now - wifiApplyAt) >= 500) beginConnection();
    if (!wifiPending && wifiAttempting && WiFi.status() == WL_CONNECTED) {
      wifiAttempting = false; wifiStatus = "Connected"; wifiMode = "ready";
    } else if (!wifiPending && wifiAttempting && uint32_t(millis() - wifiStart) >= 20000) {
      WiFi.disconnect(false);
      prefs.remove("ssid"); prefs.remove("password");
      wifiSSID = ""; wifiPassword = "";
      wifiPending = false; wifiAttempting = false; wifiMode = "setup";
      wifiStatus = "Not configured";
      wifiError = "Could not connect. Check the network name and password, then try again.";
    } else if (!wifiPending && !wifiAttempting && wifiStatus == "Connected" && WiFi.status() != WL_CONNECTED) beginConnection();
  }
  static uint32_t lastSensor = 0;
  if (uint32_t(now - lastSensor) >= 2000) {
    lastSensor = now;
    // Slow reads stay outside the lock so HTTP pump/LED commands remain responsive.
    float h = dht.readHumidity(), t = dht.readTemperature(), wt = tempSensors.getTempCByIndex(0);
    tempSensors.requestTemperatures();
    int values[4]; for (int i = 0; i < 4; ++i) values[i] = analogRead(TEMT_PIN[i]);
    StateLock lock;
    if (!isnan(h)) humidity = h;
    if (!isnan(t)) ambientTemp = t;
    if (wt != DEVICE_DISCONNECTED_C && wt >= -55 && wt <= 125) waterTemp = wt;
    int total = 0;
    for (int i = 0; i < 4; ++i) { lightIntensity[i] = values[i]; total += values[i]; }
    if (autoLight) { lightScale = nextLightScale(lightScale, total / (4.0f * 4095), lightTarget / 100.0f); writeLEDs(); }
  }
  delay(1);
}
