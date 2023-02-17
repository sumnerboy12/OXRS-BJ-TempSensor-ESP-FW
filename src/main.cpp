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
#endif

/*--------------------------- Constants -------------------------------*/
// Serial
#define   SERIAL_BAUD_RATE                115200

// Config defaults and constraints
#define   DEFAULT_TELEMETRY_INTERVAL_MS   1000
#define   TELEMETRY_INTERVAL_MS_MAX       60000

#define   ONE_WIRE_BUS                    I2C_SDA
#define   SENSOR_COUNT                    3
#define   SENSOR_RESOLUTION_BITS          9     // 9, 10, 11, or 12 bits

/*--------------------------- Global Variables ------------------------*/
// Config variables
uint32_t  telemetryIntervalMs           = DEFAULT_TELEMETRY_INTERVAL_MS;

// Telemetry variables
uint32_t  lastTelemetryMs               = 0L;
uint32_t  elapsedTelemetryMs            = 0L;

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
  telemetryIntervalMs["description"] = "How often to publish telemetry data (defaults to 1000ms, i.e. 1 second)";
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

/**
  Setup
*/
void setup() 
{
  // Start serial and let settle
  Serial.begin(SERIAL_BAUD_RATE);
  delay(1000);
  Serial.println(F("[temp] starting up..."));

  // Log the pin we are monitoring for pulse events
  oxrs.print(F("[temp] one wire bus: "));
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

  // Initialise our sensors
  for (uint8_t i = 0; i < sensors.getDS18Count(); i++)
  {
    if (i >= SENSOR_COUNT)
      break;

    if (sensors.getAddress(sensorAddress[i], i))
    {
      sensors.setResolution(sensorAddress[i], SENSOR_RESOLUTION_BITS);

      oxrs.print(F("[temp] sensor "));
      oxrs.print(i);
      oxrs.print(F(" found with address "));
      printAddress(sensorAddress[i]);
      oxrs.println();
    }
  }

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
  elapsedTelemetryMs = millis() - lastTelemetryMs;
  if (elapsedTelemetryMs >= telemetryIntervalMs)
  {
    // Request each sensor to read their temps
    sensors.requestTemperatures();

    // Build telemetry payload
    StaticJsonDocument<128> json;
    JsonArray temperatures = json.createNestedArray("temperatures");
    
    for (uint8_t i = 0; i < SENSOR_COUNT; i++)
    {
      float temperature_C = sensors.getTempC(sensorAddress[i]);
      if (temperature_C != DEVICE_DISCONNECTED_C)
      {
        JsonObject element = temperatures.createNestedObject();
        element["index"] = i;
        element["celcius"] = temperature_C;
      }
    }
    
    // Publish telemetry and reset loop variables if successful
    if (oxrs.publishTelemetry(json))
    {
      lastTelemetryMs = millis();
    }
  }
}
