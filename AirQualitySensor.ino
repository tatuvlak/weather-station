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

// Hub URL, ingest token and cycle timing. Copy config.example.h to config.h.
#include "config.h"

// Included unconditionally, unlike the block below. Even when the device is
// commissioned over BLE — so it never calls WiFi.begin() itself — it still runs
// on Wi-Fi, brought up by the Matter stack from credentials in NVS. We need the
// API to check association state and to post.
#include <WiFi.h>
#include <HTTPClient.h>


#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "esp_sleep.h"
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
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
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

unsigned long previousMillis = millis();
bool isPmsActive = false;

// ---------------------------------------------------------------------------
// Posting readings to the weather hub
// ---------------------------------------------------------------------------
// A second output alongside Matter, so the TV and phone apps can read this
// sensor without the SmartThings cloud API. Matter is untouched: the clusters
// are still updated exactly as before and the device stays commissioned, so its
// tile in the SmartThings app keeps working.
//
// Deep sleep resets the CPU and re-runs setup(), so ordinary globals do not
// survive a cycle and millis() restarts at zero. Anything that must persist
// lives in RTC memory, which does survive deep sleep (though not a power cut).

RTC_DATA_ATTR uint32_t rtcWakeCount = 0;
RTC_DATA_ATTR uint32_t rtcPushOk = 0;
RTC_DATA_ATTR uint32_t rtcPushFailed = 0;

// Readings gathered during this wake, filled in as they are taken. NAN means
// "not measured this cycle" and the field is left out of the payload rather
// than sent as a zero that would render on the dashboard as real data.
struct Reading {
    float temperature = NAN;
    float humidity = NAN;
    float pressure = NAN;
    float pm1 = NAN;
    float pm25 = NAN;
    float pm10 = NAN;
    bool  havePms = false;
};
Reading wakeReading;

// Taken at the top of the wake, before the radio and fan have had time to warm
// the board. Logged only, never sent — it exists to answer whether the reading
// taken after the PMS warm-up is skewed by self-heating, or improved by the fan
// drawing fresh air through the enclosure. Compare the two in the serial log
// over a few days and the dominant effect becomes obvious.
float coldTemperature = NAN;
float coldHumidity = NAN;

static void appendField(char *buf, size_t size, size_t &off, bool &first,
                        const char *name, const char *fmt, double value) {
    if (off >= size) return;
    int n = snprintf(buf + off, size - off, "%s\"%s\":", first ? "" : ",", name);
    if (n < 0 || (size_t)n >= size - off) { off = size; return; }
    off += n;
    n = snprintf(buf + off, size - off, fmt, value);
    if (n < 0 || (size_t)n >= size - off) { off = size; return; }
    off += n;
    first = false;
}

// Build the reading as JSON. The hub treats every field as optional, so a
// sensor that failed this cycle is omitted rather than guessed at.
static bool buildReadingJson(char *buf, size_t size, const Reading &r) {
    if (size == 0) return false;
    size_t off = 0;
    bool first = true;
    buf[off++] = '{';

    if (!isnan(r.temperature)) appendField(buf, size, off, first, "temperature_c", "%.2f", r.temperature);
    if (!isnan(r.humidity))    appendField(buf, size, off, first, "humidity_pct",  "%.2f", r.humidity);
    if (!isnan(r.pressure))    appendField(buf, size, off, first, "pressure_hpa",  "%.2f", r.pressure);

    if (r.havePms) {
        appendField(buf, size, off, first, "pm1",  "%.1f", r.pm1);
        appendField(buf, size, off, first, "pm25", "%.1f", r.pm25);
        appendField(buf, size, off, first, "pm10", "%.1f", r.pm10);
        // The air-quality enum only means anything alongside the PM values it
        // was derived from, so it travels with them.
        appendField(buf, size, off, first, "aqi", "%.0f",
                    (double)weatherStation.getAirQualityEnum());
    }

    if (first) return false;            // nothing measured; nothing to send
    if (off + 2 > size) return false;   // no room to close the object
    buf[off++] = '}';
    buf[off] = '\0';
    return true;
}

// Wait for the Matter stack to finish reassociating after wake. It reconnects
// from NVS on its own; we only decide how long to be patient.
static bool waitForWifi(unsigned long timeoutMs) {
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeoutMs) {
            Serial.printf("WiFi not connected after %lums (status %d)\r\n",
                          timeoutMs, (int)WiFi.status());
            return false;
        }
        delay(100);
    }
    Serial.printf("WiFi ready after %lums, IP %s\r\n",
                  millis() - start, WiFi.localIP().toString().c_str());
    return true;
}

static void pushReading(const Reading &r) {
    char body[224];
    if (!buildReadingJson(body, sizeof(body), r)) {
        Serial.println("Hub push skipped: nothing measured this cycle");
        return;
    }

    if (!waitForWifi(WEATHER_WIFI_WAIT_MS)) {
        rtcPushFailed++;
        return;
    }

    HTTPClient http;
    http.setConnectTimeout(WEATHER_HUB_TIMEOUT_MS);
    http.setTimeout(WEATHER_HUB_TIMEOUT_MS);
    if (!http.begin(WEATHER_HUB_URL)) {
        Serial.println("Hub push failed: bad URL");
        rtcPushFailed++;
        return;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " WEATHER_HUB_TOKEN);

    Serial.printf("Hub push: %s\r\n", body);
    int code = http.POST((uint8_t *)body, strlen(body));
    if (code == 200) {
        rtcPushOk++;
        Serial.printf("Hub push ok (%lu ok / %lu failed over %lu wakes)\r\n",
                      rtcPushOk, rtcPushFailed, rtcWakeCount);
    } else if (code > 0) {
        // The hub explains rejections in the body, and this is read off a
        // serial console with no debugger attached.
        rtcPushFailed++;
        Serial.printf("Hub push rejected: HTTP %d %s\r\n", code, http.getString().c_str());
    } else {
        rtcPushFailed++;
        Serial.printf("Hub push failed: %s\r\n", HTTPClient::errorToString(code).c_str());
    }
    http.end();
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
    // Set BME280 to forced mode and oversampling 1x to reduce self-heating
    bme.setSampling(
        Adafruit_BME280::MODE_FORCED,
        Adafruit_BME280::SAMPLING_X1, // temperature
        Adafruit_BME280::SAMPLING_X1, // pressure
        Adafruit_BME280::SAMPLING_X1, // humidity
        Adafruit_BME280::FILTER_OFF
    );
    
    // Reference reading taken now, while the board is closest to ambient after
    // deep sleep. Logged only — see coldTemperature's declaration for why.
    bme.takeForcedMeasurement();
    coldTemperature = bme.readTemperature();
    coldHumidity = bme.readHumidity();
    rtcWakeCount++;
    Serial.printf("Wake #%lu - cold reference: %.2f C, %.2f %%\r\n",
                  rtcWakeCount, coldTemperature, coldHumidity);

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
    pms.wakeUp();
    Serial.printf("Wait %d seconds for PMS to wake up...", PMS_WARMUP_SECONDS);
    unsigned long currentMillis = millis();
    while(currentMillis - previousMillis < (unsigned long)PMS_WARMUP_SECONDS * 1000UL)
    {
        delay(100);
        currentMillis = millis();
        Serial.print(".");
    }
    // Take a forced measurement to minimize self-heating
    bme.takeForcedMeasurement();

    float temperature = bme.readTemperature();
    Serial.print("Setup Temperature = ");
    Serial.print(temperature);
    Serial.println(" °C");        

    float pressure = bme.readPressure() / 100.0F;
    Serial.print("Setup Pressure = ");
    Serial.print(pressure);
    Serial.println(" hPa");        

    float humidity = bme.readHumidity();
    Serial.print("Setup Humidity = ");
    Serial.print(humidity);
    Serial.println(" %"); 
    Serial.println("Setup Reading PMS data");
    pms.requestRead();
    float PM1 = 0.0;
    float PM25 = 0.0;
    float PM10 = 0.0;
    if (pms.readUntil(data))
    {
        Serial.println("Setup PMS data recieved");
        PM1 = data.PM_AE_UG_1_0;
        Serial.printf("PM1: %.1f ppm\r\n", PM1);
        PM25 = data.PM_AE_UG_2_5;
        Serial.printf("PM2.5: %.1f ppm\r\n", PM25);
        PM10 = data.PM_AE_UG_10_0;
        Serial.printf("PM10: %.1f ppm\r\n", PM10);         
    }
    pms.sleep();
    weatherStation.begin(temperature, humidity, pressure, PM1, PM25, PM10);

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
    
    previousMillis = millis();
    Serial.println("Setup finished");
}


void loop()
{
    static uint32_t counter = 0;
    
    unsigned long currentMillis = millis();
    if(!isPmsActive)
    {
        pms.wakeUp();
        isPmsActive = true;

        // Take a forced measurement to minimize self-heating
        bme.takeForcedMeasurement();

        float temperature = bme.readTemperature();
        //temperatureSensor.setTemperature(temperature);
        weatherStation.setTemperature(temperature);
        Serial.print("Temperature = ");
        Serial.print(temperature);
        Serial.println(" °C");        

        float pressure = bme.readPressure() / 100.0F;
        //pressureSensor.setPressure(pressure);
        weatherStation.setPressure(pressure);
        Serial.print("Pressure = ");
        Serial.print(pressure);
        Serial.println(" hPa");        

        float humidity = bme.readHumidity();
        //humiditySensor.setHumidity(humidity);
        weatherStation.setHumidity(humidity);
        Serial.print("Humidity = ");
        Serial.print(humidity);
        Serial.println(" %"); 

        // Keep these for the hub push at the end of the wake.
        wakeReading.temperature = temperature;
        wakeReading.pressure = pressure;
        wakeReading.humidity = humidity;
        Serial.printf("Warm vs cold: %.2f C (warm) - %.2f C (cold) = %+.2f C\r\n",
                      temperature, coldTemperature, temperature - coldTemperature);
    }
        
    if (currentMillis - previousMillis >= (unsigned long)PMS_WARMUP_SECONDS * 1000UL) 
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

            // setPM10 was the last setter, so the air-quality enum is current.
            wakeReading.pm1 = PM1;
            wakeReading.pm25 = PM25;
            wakeReading.pm10 = PM10;
            wakeReading.havePms = true;
        }
        pms.sleep();
        isPmsActive = false;      
        
    }    

    //wait 3 seconds before going to deep sleep - IF PMS is inactive
    if (!isPmsActive) 
    {   
        // Every reading for this wake now exists, and the device is about to go
        // dark for minutes — this is the last chance to post. Failures are
        // swallowed inside pushReading, so a NAS mid-reboot cannot stop the
        // device sleeping or disturb the Matter side.
        pushReading(wakeReading);

        previousMillis = currentMillis;
        // Set wakeup timer for 300 seconds and enter deep sleep
        esp_sleep_enable_timer_wakeup((uint64_t)SENSOR_SLEEP_SECONDS * 1000000ULL);
        Serial.println("Entering deep sleep in 3 seconds...");
        while(currentMillis - previousMillis < (unsigned long)10000)
        {
            delay(100);
            currentMillis = millis();
            Serial.print(".");
        }
        Serial.flush();
        esp_deep_sleep_start();
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