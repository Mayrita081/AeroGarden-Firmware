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
#include <time.h>
#include <sys/time.h>

// Confirmed wiring: four ambient-only sensors around each floor.
// GPIO 5 is a separate LED, intentionally left untouched.
constexpr int NEO_PIN = 21, PUMP_PIN = 20, DHT_PIN = 7, DS18B20_PIN = 8;
constexpr int TEMT_PIN[] = {0, 1, 2, 3};
constexpr int MAX_FLOORS = 3;
Adafruit_NeoPixel strip(LEDS_PER_FLOOR * MAX_FLOORS, NEO_PIN, NEO_GRB + NEO_KHZ800);
OneWire oneWire(DS18B20_PIN);
DallasTemperature tempSensors(&oneWire);
DHT dht(DHT_PIN, DHT22);
AsyncWebServer server(80);
Preferences prefs;
SemaphoreHandle_t stateMutex;
struct StateLock {
  StateLock() { xSemaphoreTake(stateMutex, portMAX_DELAY); }
  ~StateLock() { xSemaphoreGive(stateMutex); }
};
PumpControl pump;
DailyLightSchedule ledSchedule;
bool lastLightAllowed = true;
bool ntpStarted = false;
int activeFloors = 1, redIntensity = 0, blueIntensity = 0;
bool autoLight = false;
int lightTarget = 50;
Color sectionOutput[LIGHT_SENSOR_COUNT] = {};
int actualRed = 0, actualBlue = 0;
float humidity = NAN, ambientTemp = NAN, waterTemp = NAN;
int lightIntensity[4] = {};
String wifiSSID, wifiPassword, wifiStatus = "Sin configurar";
String wifiMode = "setup", wifiError;
bool wifiPending = false, wifiAttempting = false;
uint32_t wifiStart = 0, wifiApplyAt = 0;

bool clockValid(time_t epoch) { return epoch >= 1704067200LL; }
bool lightAllowed(time_t epoch) {
  return ledSchedule.allows(clockValid(epoch), localMinute(epoch, ledSchedule.utcOffsetMinutes));
}
void writeLEDs() {
  lastLightAllowed = lightAllowed(time(nullptr));
  actualRed = 0; actualBlue = 0;
  for (int sensor = 0; sensor < LIGHT_SENSOR_COUNT; ++sensor) {
    sectionOutput[sensor] = sectionColor(redIntensity, blueIntensity, autoLight,
      lightIntensity[sensor], lightTarget, lastLightAllowed);
    actualRed += sectionOutput[sensor].red;
    actualBlue += sectionOutput[sensor].blue;
  }
  // Legacy aggregate fields are averages; sectionOutput is the authoritative output.
  actualRed /= LIGHT_SENSOR_COUNT; actualBlue /= LIGHT_SENSOR_COUNT;
  for (int px = 0; px < strip.numPixels(); ++px) {
    const Color color = sectionOutput[sensorForPixel(px)];
    strip.setPixelColor(px, px < activeFloors * LEDS_PER_FLOOR
      ? strip.Color(color.red, 0, color.blue) : 0);
  }
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
  StaticJsonDocument<3072> doc;
  doc["humidity"] = humidity; doc["ambientTemp"] = ambientTemp; doc["waterTemp"] = waterTemp;
  auto light = doc.createNestedArray("light");
  for (int value : lightIntensity) light.add(value);
  doc["pump"] = pump.on ? 1 : 0;
  doc["pumpMode"] = pump.mode == PumpMode::Cycle ? "cycle" : pump.mode == PumpMode::Timer ? "timer" : "manual";
  doc["remainingSeconds"] = pump.remaining(millis()) / 1000;
  doc["onMinutes"] = pump.onMinutes; doc["offMinutes"] = pump.offMinutes;
  doc["red"] = redIntensity; doc["blue"] = blueIntensity;
  doc["actualRed"] = actualRed; doc["actualBlue"] = actualBlue;
  auto outputs = doc.createNestedArray("sectionOutput");
  for (int sensor = 0; sensor < LIGHT_SENSOR_COUNT; ++sensor) {
    auto output = outputs.createNestedObject();
    output["red"] = sectionOutput[sensor].red; output["blue"] = sectionOutput[sensor].blue;
  }
  doc["autoLight"] = autoLight; doc["lightTarget"] = lightTarget; doc["floors"] = activeFloors;
  const time_t epoch = time(nullptr);
  doc["clockValid"] = clockValid(epoch);
  doc["localMinute"] = clockValid(epoch) ? localMinute(epoch, ledSchedule.utcOffsetMinutes) : -1;
  doc["ledScheduleEnabled"] = ledSchedule.enabled;
  doc["ledStartMinute"] = ledSchedule.startMinute; doc["ledEndMinute"] = ledSchedule.endMinute;
  doc["utcOffsetMinutes"] = ledSchedule.utcOffsetMinutes;
  doc["ledWindowActive"] = lightAllowed(epoch);
  doc["uptime"] = millis() / 1000;
  doc["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  doc["wifiStatus"] = wifiStatus; doc["ssid"] = wifiSSID;
  doc["wifiMode"] = wifiMode; doc["wifiError"] = wifiError;
  String out; serializeJson(doc, out); return out;
}
bool integer(JsonVariant v, int low, int high) { return v.is<int>() && v.as<int>() >= low && v.as<int>() <= high; }
void bad(AsyncWebServerRequest* req, const char* message) {
  StaticJsonDocument<384> doc; doc["error"] = message;
  String out; serializeJson(doc, out); req->send(400, "application/json", out);
}
void ok(AsyncWebServerRequest* req) { req->send(200, "application/json", snapshot()); }
void post(const char* path, ArJsonRequestHandlerFunction callback) {
  auto handler = new AsyncCallbackJsonWebHandler(path, [callback, path](AsyncWebServerRequest* req, JsonVariant& json) {
    if (req->url() != path) { req->send(404); return; }
    if (!json.is<JsonObject>()) { bad(req, "Se esperaba un objeto JSON"); return; }
    StateLock lock;
    if (req->url() != "/api/wifi" && wifiMode != "ready") {
      req->send(409, "application/json", "{\"error\":\"Primero completá la configuración Wi-Fi\"}"); return;
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
  // Register child route before /api/leds (AsyncJson also matches child paths).
  post("/api/leds/schedule", [](AsyncWebServerRequest* req, JsonVariant& j) {
    if (!j["enabled"].is<bool>() || !integer(j["startMinute"], 0, 1439) ||
        !integer(j["endMinute"], 0, 1439) || !integer(j["utcOffsetMinutes"], -720, 840)) {
      bad(req, "Elegí horas válidas y una diferencia con UTC de entre -720 y 840 minutos"); return;
    }
    DailyLightSchedule next;
    next.enabled = j["enabled"]; next.startMinute = j["startMinute"]; next.endMinute = j["endMinute"];
    next.utcOffsetMinutes = j["utcOffsetMinutes"];
    if (!next.valid()) { bad(req, "Las horas de inicio y fin deben ser distintas"); return; }
    StaticJsonDocument<256> saved;
    saved["enabled"] = next.enabled; saved["startMinute"] = next.startMinute;
    saved["endMinute"] = next.endMinute; saved["utcOffsetMinutes"] = next.utcOffsetMinutes;
    String encoded; serializeJson(saved, encoded);
    if (prefs.putString("ledSchedule", encoded) != encoded.length()) {
      req->send(500, "application/json", "{\"error\":\"No se pudo guardar el horario LED\"}"); return;
    }
    ledSchedule = next; writeLEDs(); ok(req);
  });
  post("/api/time", [](AsyncWebServerRequest* req, JsonVariant& j) {
    if (!j["epoch"].is<int64_t>() || j["epoch"].as<int64_t>() < 1704067200LL ||
        j["epoch"].as<int64_t>() > 4102444799LL) {
      bad(req, "Ingresá una fecha UTC válida entre 2024 y 2099"); return;
    }
    timeval tv = {}; tv.tv_sec = j["epoch"].as<int64_t>();
    if (settimeofday(&tv, nullptr) != 0) {
      req->send(500, "application/json", "{\"error\":\"No se pudo configurar el reloj\"}"); return;
    }
    writeLEDs(); ok(req);
  });
  post("/api/leds", [](AsyncWebServerRequest* req, JsonVariant& j) {
    if (!integer(j["red"], 0, 255) || !integer(j["blue"], 0, 255) || !j["autoLight"].is<bool>() || !integer(j["lightTarget"], 1, 100)) {
      bad(req, "Ajustá rojo y azul entre 0 y 255, y el objetivo de luz entre 1 y 100"); return;
    }
    if (j["autoLight"].as<bool>() && j["red"].as<int>() + j["blue"].as<int>() == 0) {
      bad(req, "Elegí una mezcla de color distinta de cero para la iluminación automática"); return;
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
    else { bad(req, "Usá un temporizador o ciclo con duraciones de entre 1 y 10080 minutos"); return; }
    writePump(); saveControls(); ok(req);
  });
  post("/api/pump", [](AsyncWebServerRequest* req, JsonVariant& j) {
    if (!integer(j["state"], 0, 1)) { bad(req, "El estado debe ser 0 o 1"); return; }
    pump.manual(j["state"].as<int>() == 1); writePump(); saveControls(); ok(req);
  });
  post("/api/settings", [](AsyncWebServerRequest* req, JsonVariant& j) {
    if (!integer(j["floors"], 1, MAX_FLOORS)) { bad(req, "La cantidad de pisos debe ser de 1 a 3"); return; }
    activeFloors = j["floors"]; writeLEDs(); saveControls(); ok(req);
  });
  post("/api/wifi", [](AsyncWebServerRequest* req, JsonVariant& j) {
    if (wifiMode == "connecting") {
      req->send(409, "application/json", "{\"error\":\"Ya hay un intento de conexión en curso\"}"); return;
    }
    if (!j["ssid"].is<const char*>() || !j["password"].is<const char*>()) { bad(req, "Se requieren el nombre de la red y la contraseña"); return; }
    String ssid = j["ssid"], password = j["password"];
    if (ssid.length() < 1 || ssid.length() > 32 || (password.length() != 0 && (password.length() < 8 || password.length() > 63))) {
      bad(req, "Nombre de red: 1–32 bytes; contraseña: 8–63 bytes, o vacía si la red es abierta"); return;
    }
    // Persist before connecting; failed attempts return to a clean setup flow.
    prefs.remove("ssid");
    prefs.putString("password", password); prefs.putString("ssid", ssid);
    if (prefs.getString("ssid", "") != ssid || prefs.getString("password", "") != password) {
      prefs.remove("ssid"); prefs.remove("password");
      req->send(500, "application/json", "{\"error\":\"No se pudieron guardar los datos de conexión. Volvé a intentarlo.\"}"); return;
    }
    wifiMode = "connecting"; wifiError = "";
    wifiSSID = ssid; wifiPassword = password;
    wifiPending = true; wifiApplyAt = millis(); wifiStatus = "Conectando"; ok(req);
  });
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");
  server.onNotFound([](AsyncWebServerRequest* req) { req->send(404, "application/json", "{\"error\":\"No encontrado\"}"); });
}
void beginConnection() {
  wifiMode = "connecting"; wifiError = "";
  WiFi.disconnect(false); WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  wifiStart = millis(); wifiAttempting = true; wifiPending = false; wifiStatus = "Conectando";
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
  // One Preferences record keeps schedule fields consistent across a power loss.
  StaticJsonDocument<256> savedSchedule;
  if (!deserializeJson(savedSchedule, prefs.getString("ledSchedule", ""))) {
    DailyLightSchedule saved;
    saved.enabled = savedSchedule["enabled"] | false;
    saved.startMinute = savedSchedule["startMinute"] | 360;
    saved.endMinute = savedSchedule["endMinute"] | 1080;
    saved.utcOffsetMinutes = savedSchedule["utcOffsetMinutes"] | -180;
    if (saved.valid()) ledSchedule = saved;
  }
  // Boot starts a full OFF interval for repeating schedules; one-shot timers reset.
  analogReadResolution(12);
  for (int i = 0; i < LIGHT_SENSOR_COUNT; ++i) {
    pinMode(TEMT_PIN[i], INPUT);
    lightIntensity[i] = analogRead(TEMT_PIN[i]);
  }
  writeLEDs();
  dht.begin();
  tempSensors.begin();
  tempSensors.setWaitForConversion(false);
  tempSensors.requestTemperatures();
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
    if (lightAllowed(time(nullptr)) != lastLightAllowed) writeLEDs();
    if (wifiPending && uint32_t(now - wifiApplyAt) >= 500) beginConnection();
    if (!wifiPending && wifiAttempting && WiFi.status() == WL_CONNECTED) {
      wifiAttempting = false; wifiStatus = "Conectado"; wifiMode = "ready";
      if (!ntpStarted) {
        // SNTP syncs asynchronously; all scheduling uses UTC plus the saved offset.
        configTime(0, 0, "pool.ntp.org", "time.google.com"); ntpStarted = true;
      }
    } else if (!wifiPending && wifiAttempting && uint32_t(millis() - wifiStart) >= 20000) {
      WiFi.disconnect(false);
      prefs.remove("ssid"); prefs.remove("password");
      wifiSSID = ""; wifiPassword = "";
      wifiPending = false; wifiAttempting = false; wifiMode = "setup";
      wifiStatus = "Sin configurar";
      wifiError = "No se pudo conectar. Revisá el nombre de la red y la contraseña y volvé a intentarlo.";
    } else if (!wifiPending && !wifiAttempting && wifiStatus == "Conectado" && WiFi.status() != WL_CONNECTED) beginConnection();
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
    for (int i = 0; i < LIGHT_SENSOR_COUNT; ++i) lightIntensity[i] = values[i];
    if (autoLight) writeLEDs();
  }
  delay(1);
}
