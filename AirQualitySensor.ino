// Copyright 2025 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*
 * This example is an example code that will create a Matter Device which can be
 * commissioned and controlled from a Matter Environment APP.
 * Additionally the ESP32 will send debug messages indicating the Matter activity.
 * Turning DEBUG Level ON may be useful to following Matter Accessory and Controller messages.
 */

//TODO: remove CO2 references; remove ssid and pass, refactor - remove AQI as separate file - should be part of MatterAirQualitySensor



#include <Matter.h>
#include <esp_matter.h> // Include the underlying SDK
#include "nvs_flash.h"
#if !CONFIG_ENABLE_CHIPOBLE
// if the device can be commissioned using BLE, WiFi is not used - save flash space
#include <WiFi.h>
#endif
//#include <MatterEndpoints/MatterAirQualitySensor.h>
#include "CopyTo_esp32_library_MatterEndpoints/MatterAirQualitySensor.h"
#include "CopyTo_esp32_library_MatterEndpoints/MatterWeatherStation.h"
#include "PMS.h"


#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#define USEIIC 1
#define BME_SDA 6
//#define BME_MISO 12
#define BME_SCL 7
//#define BME_CS 10
Adafruit_BME280 bme; // I2C

#define PMS_BAUD 9600
// Create an instance of the HardwareSerial class for Serial 2
HardwareSerial pmsSerial(2);

PMS pms(pmsSerial);
PMS::DATA data;

// Custom Matter Components
/* MatterAirQualitySensor airSensor;
MatterTemperatureSensor temperatureSensor;
MatterPressureSensor pressureSensor;
MatterHumiditySensor humiditySensor;
 */
MatterWeatherStation weatherStation;

#if !CONFIG_ENABLE_CHIPOBLE
// WiFi is manually set and started
const char *ssid = "YOUR_SSID_HERE";
const char *password = "YOUR_PASSWORD_HERE";
#endif
const uint8_t buttonPin = BOOT_PIN;

uint32_t button_time_stamp = 0;
bool button_state = false;
const uint32_t decommissioningTimeout = 5000;

// Helper function to update the identity
void updateMatterIdentity() {
  uint16_t endpoint_id = 0; // Root Node
  
  // Attribute IDs for Basic Information Cluster (0x0028)
  uint32_t cluster_id = chip::app::Clusters::BasicInformation::Id;
  
  auto update_attr = [&](uint32_t attr_id, const char* value) {
    esp_matter_attr_val_t val = esp_matter_char_str((char*)value, strlen(value));
    esp_err_t err = attribute::update(endpoint_id, cluster_id, attr_id, &val);
    if (err != ESP_OK) {
      Serial.printf("Failed to update attr 0x%lx: error 0x%x\n", attr_id, err);
    }
  };

  Serial.println("Updating Matter Identity...");

  // Update Vendor Name
  update_attr(chip::app::Clusters::BasicInformation::Attributes::VendorName::Id, "BarTech");

  // Update Product Name
  update_attr(chip::app::Clusters::BasicInformation::Attributes::ProductName::Id, "AirQuality-C6");

  // Update Serial Number
  update_attr(chip::app::Clusters::BasicInformation::Attributes::SerialNumber::Id, "SN-12345678");

  // Update Node Label (The friendly name)
  update_attr(chip::app::Clusters::BasicInformation::Attributes::NodeLabel::Id, "Air Quality Sensor");
}

void setup()
{
    pinMode(buttonPin, INPUT_PULLUP); 
    Serial.begin(115200);    

    Wire.begin(BME_SDA, BME_SCL);
    if (!bme.begin(0x77, &Wire)) {
        Serial.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
        Serial.print("SensorID was: 0x"); Serial.println(bme.sensorID(),16);
        Serial.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
        Serial.print("   ID of 0x56-0x58 represents a BMP 280,\n");
        Serial.print("        ID of 0x60 represents a BME 280.\n");
        Serial.print("        ID of 0x61 represents a BME 680.\n");
    }
    
    pmsSerial.begin(PMS_BAUD);
    pms.passiveMode();
 
// CONFIG_ENABLE_CHIPOBLE is enabled when BLE is used to commission the Matter Network
#if !CONFIG_ENABLE_CHIPOBLE
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
#endif
    // Initialize all custom endpoints   
    /* pressureSensor.begin(900.00);
    temperatureSensor.begin();    
    humiditySensor.begin(95.00);
    airSensor.begin(); */
    weatherStation.begin(0.0, 0.0, 900.0, 0.0, 0.0, 0.0);

    // Start Matter stack after all endpoints
    Matter.begin();
    updateMatterIdentity();

    // Commissioning check
    Serial.printf("Is commisioned: %s\r\n",Matter.isDeviceCommissioned() ? "true" : "false");
    if (!Matter.isDeviceCommissioned())
    {
        Serial.println("Matter Node is not yet commissioned.");
        Serial.printf("Manual pairing code: %s\r\n", Matter.getManualPairingCode().c_str());
        Serial.printf("QR code URL: %s\r\n", Matter.getOnboardingQRCodeUrl().c_str());

        while (!Matter.isDeviceCommissioned())
        {
            delay(100);
            Serial.println("Waiting for commissioning...");
        }

        Serial.println("Matter Node successfully commissioned.");
    }
    
    Serial.println("Setup finished");
}

unsigned long previousMillis = millis();
bool pmsWanken = false;

void loop()
{
    static uint32_t counter = 0;
    
    unsigned long currentMillis = millis();
    if(!pmsWanken)
    {
        pms.wakeUp();
        pmsWanken = true;
    }
        
    if (currentMillis - previousMillis >= (unsigned long)30000) 
    {   
        previousMillis = currentMillis;
        Serial.println(".");
        Serial.println("Reading PMS data");
        pms.requestRead();
        if (pms.readUntil(data))
        {
            Serial.println("PMS data recieved");
            float PM1 = data.PM_AE_UG_1_0;
            Serial.printf("PM1: %.1f ppm\r\n", PM1);
            //airSensor.setPM1(PM1);
            weatherStation.setPM1(PM1);
            float PM25 = data.PM_AE_UG_2_5;
            Serial.printf("PM2.5: %.1f ppm\r\n", PM25);
            //airSensor.setPM2_5(PM25);
            weatherStation.setPM2_5(PM25);
            float PM10 = data.PM_AE_UG_10_0;
            Serial.printf("PM10: %.1f ppm\r\n", PM10);
            //airSensor.setPM10(PM10);
            weatherStation.setPM10(PM10);
            Serial.printf("PM concentration: set\r\n");
        }
        pms.sleep();
        pmsWanken = false;
        
        float pressure = bme.readPressure() / 100.0F;
        //pressureSensor.setPressure(pressure);
        weatherStation.setPressure(pressure);
        Serial.print("Pressure = ");
        Serial.print(pressure);
        Serial.println(" hPa");

        float temperature = bme.readTemperature();
//        temperatureSensor.setTemperature(temperature);
        weatherStation.setTemperature(temperature);
        Serial.print("Temperature = ");
        Serial.print(temperature);
        Serial.println(" °C");        

        float humidity = bme.readHumidity();
        //humiditySensor.setHumidity(humidity);
        weatherStation.setHumidity(humidity);
        Serial.print("Humidity = ");
        Serial.print(humidity);
        Serial.println(" %"); 
    }

    // Button debounce and long-press for decommissioning
    if (digitalRead(buttonPin) == LOW && !button_state)
    {
        button_time_stamp = millis();
        button_state = true;
    }

    if (digitalRead(buttonPin) == HIGH && button_state)
    {
        button_state = false;
    }

    if (button_state && (millis() - button_time_stamp) > decommissioningTimeout)
    {
        Serial.println("Decommissioning Matter Node.");
        Matter.decommission();
        // Use the official Matter factory reset method
        Serial.println("Performing Factory Reset...");
        esp_matter::factory_reset();
        ESP.restart();
    }
    delay(100);
}