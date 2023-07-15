/**
  Flow sensor firmware for the Open eXtensible Rack System
  
  GitHub repository:
    https://github.com/sumnerboy12/OXRS-BJ-TempSensor-ESP-FW
    
  Copyright 2023 Ben Jones <ben.jones12@gmail.com>
*/

/*--------------------------- Libraries -------------------------------*/
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#if defined(OXRS_ROOM8266)
#include <OXRS_Room8266.h>
OXRS_Room8266 oxrs;

#elif defined(OXRS_LILYGO)
#include <OXRS_LILYGOPOE.h>
OXRS_LILYGOPOE oxrs;
#endif

/*--------------------------- Constants -------------------------------*/
// Serial
#define   SERIAL_BAUD_RATE                115200

// Config defaults and constraints
#define   DEFAULT_TELEMETRY_INTERVAL_MS   5000
#define   TELEMETRY_INTERVAL_MS_MAX       60000

// Default OneWire bus to I2C SDA pin so we can use existing OXRS
// IDC I2C breakout headers typically found on most boards/shields
#ifndef   ONE_WIRE_BUS
#define   ONE_WIRE_BUS                    I2C_SDA
#endif

// Support up to a maximum 5x sensors
#define   SENSOR_COUNT                    5

// TODO: Make sensor resolution configurable?
#define   SENSOR_RESOLUTION_BITS          9     // 9, 10, 11, or 12 bits

/*--------------------------- Global Variables ------------------------*/
// Config variables
uint32_t  telemetryIntervalMs           = DEFAULT_TELEMETRY_INTERVAL_MS;

// Telemetry variables
uint32_t  lastTelemetryMs               = 0L;
uint32_t  elapsedTelemetryMs            = 0L;

// Publish Home Assistant self-discovery config for each sensor
bool g_hassDiscoveryPublished           = false;

/*--------------------------- Instantiate Globals ---------------------*/
// OneWire bus
OneWire oneWire(ONE_WIRE_BUS);

// DS18B20 sensors
DallasTemperature sensors(&oneWire);
DeviceAddress sensorAddress[SENSOR_COUNT];

/*--------------------------- Program ---------------------------------*/
void setConfigSchema()
{
  // Define our config schema
  StaticJsonDocument<1024> json;
  
  JsonObject telemetryIntervalMs = json.createNestedObject("telemetryIntervalMs");
  telemetryIntervalMs["title"] = "Telemetry Interval (ms)";
  telemetryIntervalMs["description"] = "How often to publish telemetry data (defaults to 5000ms, i.e. 5 seconds)";
  telemetryIntervalMs["type"] = "integer";
  telemetryIntervalMs["minimum"] = 1;
  telemetryIntervalMs["maximum"] = TELEMETRY_INTERVAL_MS_MAX;

  // Pass our config schema down to the Room8266 library
  oxrs.setConfigSchema(json.as<JsonVariant>());
}

void jsonConfig(JsonVariant json)
{
  if (json.containsKey("telemetryIntervalMs"))
  {
    telemetryIntervalMs = min(json["telemetryIntervalMs"].as<int>(), TELEMETRY_INTERVAL_MS_MAX);
  }
}

void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    // zero pad the address if necessary
    if (deviceAddress[i] < 16) oxrs.print(F("0"));
    oxrs.print(deviceAddress[i], HEX);
  }
}

void initialiseSensors()
{
  // Log the pin we are using for the OneWire bus
  oxrs.print(F("[temp] one wire bus on GPIO "));
  oxrs.println(ONE_WIRE_BUS);

  // Start sensor library
  sensors.begin();

  // Log how many sensors we found on the bus
  oxrs.print(F("[temp] "));
  oxrs.print(sensors.getDS18Count());
  oxrs.println(F(" ds18b20s found"));

  if (sensors.getDS18Count() > SENSOR_COUNT)
  {
    oxrs.print(F("[temp] too many ds18b20s, only support a max of "));
    oxrs.println(SENSOR_COUNT);
  }

  // Initialise sensors
  for (uint8_t i = 0; i < sensors.getDS18Count(); i++)
  {
    if (i >= SENSOR_COUNT)
      break;

    if (sensors.getAddress(sensorAddress[i], i))
    {
      oxrs.print(F("[temp] sensor "));
      oxrs.print(i);
      oxrs.print(F(" found with address "));
      printAddress(sensorAddress[i]);
      oxrs.println();

      // Set the sensor resolution
      sensors.setResolution(sensorAddress[i], SENSOR_RESOLUTION_BITS);
    }
  }
}

void publishTelemetry()
{
  StaticJsonDocument<128> json;

  char key[8];
  for (uint8_t i = 0; i < SENSOR_COUNT; i++)
  {
    float tempC = sensors.getTempC(sensorAddress[i]);
    if (tempC == DEVICE_DISCONNECTED_C)
      continue;

    sprintf_P(key, PSTR("temp%d"), i);
    json[key] = tempC;
  }

  oxrs.publishTelemetry(json);
}

void publishHassDiscovery()
{
  if (g_hassDiscoveryPublished)
    return;

  char component[8];
  sprintf_P(component, PSTR("sensor"));

  char sensorId[8];
  char sensorName[8];

  char telemetryTopic[64];
  char valueTemplate[32];

  for (uint8_t i = 0; i < SENSOR_COUNT; i++)
  {
    // JSON config payload (empty if the sensor is not found, to clear any existing config)
    DynamicJsonDocument json(1024);

    sprintf_P(sensorId, PSTR("temp%d"), i);

    float tempC = sensors.getTempC(sensorAddress[i]);
    if (tempC != DEVICE_DISCONNECTED_C)
    {
      oxrs.getHassDiscoveryJson(json, sensorId);

      sprintf_P(sensorName, PSTR("Temp %d"), i);
      json["name"]  = sensorName;

      json["dev_cla"] = "temperature";
      json["unit_of_meas"] = "°C";

      json["stat_t"] = oxrs.getMQTT()->getTelemetryTopic(telemetryTopic);

      sprintf_P(valueTemplate, PSTR("{{ value_json.%s }}"), sensorId);
      json["val_tpl"] = valueTemplate;
    }

    oxrs.publishHassDiscovery(json, component, sensorId);
  }

  // Only publish once on boot
  g_hassDiscoveryPublished = true;
}

/**
  Setup
*/
void setup() 
{
  // Start serial and let settle
  Serial.begin(SERIAL_BAUD_RATE);
  delay(1000);
  Serial.println(F("[temp] starting up..."));

  // Discover and initialise sensors
  initialiseSensors();

  // Start hardware
  oxrs.begin(jsonConfig, NULL);

  // Set up config schema (for self-discovery and adoption)
  setConfigSchema();
}

/**
  Main processing loop
*/
void loop() 
{
  // Let hardware handle any events etc
  oxrs.loop();

  // Check if we need to send telemetry
  if (millis() - lastTelemetryMs >= telemetryIntervalMs)
  {
    // Send request to OneWire bus asking sensors to read temps
    sensors.requestTemperatures();

    // Publish temperature telemetry data
    publishTelemetry();

    // Reset telemetry timer
    lastTelemetryMs = millis();
  }

  // Check if we need to publish any Home Assistant discovery payloads
  if (oxrs.isHassDiscoveryEnabled())
  {
    publishHassDiscovery();
  }
}
