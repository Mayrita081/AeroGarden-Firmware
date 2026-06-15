#include <Arduino.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>


//Bluetooth Serial instance
BluetoothSerial SerialBT;

//PIN DEFINITIONS FOR LEDS & Water Pump
const int pinRed[3] = {17, 21, 25}; // Pin for Red LED Floor 1
const int pinBlue[3] = {19, 23, 27}; // Pin for Blue LED Floor 1

const int RedChannel[3] = {2, 4, 6}; // PWM channel for Red LED
const int BlueChannel[3] = {1, 3, 5}; // PWM channel for Blue LED

int RedIntensity[3] = {0, 0, 0}; // Intensity for Red LED
int BlueIntensity[3] = {0, 0, 0}; // Intensity for Blue LED

int FloorQty = 1; // Number of floors in the system

const int pumpPin = 5; // Pin for water pump

//PIN DEFINITIONS FOR SENSORS
const int DHTPin = 32; // Pin for Ambient humidity and temperature sensor
const int DS18B20Pin = 33; // Pin for water temperature sensor
const int TEMT6000Pin[4] = {36, 36, 36, 36}; // Pin for ambient light sensor

// VARIABLES TO STORE SENSOR READINGS
float humidity = 0.0;
float temperatureC = 0.0;
float waterTemperature = 0.0;
int lightIntensity[4] = {0, 0, 0, 0};

// Library instances
OneWire oneWire(DS18B20Pin); // Setup OneWire for DS18B20
DallasTemperature sensors(&oneWire); // Pass OneWire reference to DallasTemperature
DHT dht(DHTPin, DHT22);


//Function Prototypes
void readSensors();
void writeLEDs();
void SerialPrints();

void setup() {
  Serial.begin(115200); // Start serial communication at 115200 baud rate

//Bluetooth Serial setup
  SerialBT.begin("AeroGarden"); // Bluetooth device name
  Serial.println("Dispositivo Bluetooth iniciado, esperando conexión...");
// Set LED pins as OUTPUT
  for (int i = 0; i <= FloorQty; i++){
      pinMode(pinRed[i], OUTPUT);
      pinMode(pinBlue[i], OUTPUT);
      Serial.println(i);
  }
  for (int i = 0; i <= FloorQty; i++){
      ledcAttachPin(pinRed[i], RedChannel[i]); // Attach Red LED pin to channel 0
      ledcAttachPin(pinBlue[i], BlueChannel[i]); // Attach Blue LED pin to channel 1
  }
  
  pinMode(pumpPin, OUTPUT); // Set water pump pin as OUTPUT

  pinMode(DHTPin, INPUT); // Set DHT sensor pin as INPUT
  pinMode(DS18B20Pin, INPUT); // Set DS18B20 sensor pin as INPUT
  for (int i = 0; i < 4; i++) {
    pinMode(TEMT6000Pin[i], INPUT); // Set TEMT6000 sensor pin as INPUT
  }

  //Initialize DHT sensor
  dht.begin();

  // Initialize DS18B20 sensor
  sensors.begin();
}

void loop() {
  // put your main code here, to run repeatedly:
  readSensors();
  SerialPrints();
  //writeLEDs();

}

// put function definitions here:
void readSensors() {
  
  // Read humidity
  humidity = dht.readHumidity();
  
  // Read temperature in Celsius
  temperatureC = dht.readTemperature();

  // Read water temperature from DS18B20 sensor
  sensors.requestTemperatures(); // Send command to get temperatures
  waterTemperature = sensors.getTempCByIndex(0);

  // Read ambient light intensity from TEMT6000 sensor
  for (int i = 0; i < 4; i++) {
    lightIntensity[i] = analogRead(TEMT6000Pin[i]);
  }

}

void writeLEDs() {
  // Example logic to control LEDs based on sensor readings
  for (int i = 0; i < FloorQty; i++) {
    // Control Red LED based on temperature
    ledcWrite(RedChannel[i], RedIntensity[i]); 
    ledcWrite(BlueChannel[i], BlueIntensity[i]);
}
}

void SerialPrints() {
Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.print(" %\t");
  Serial.print("Temperature: ");
  Serial.print(temperatureC);
  Serial.print(" °C\t");
  Serial.print("Light Intensity: ");
  for (int i = 0; i < 4; i++) {
    Serial.print(lightIntensity[i]);
    if (i < 3) Serial.print(", ");
  }
  Serial.print("\t");
  Serial.print("Water Temperature: ");
  Serial.print(waterTemperature);
  Serial.print(" °C");
  Serial.println();
  }

