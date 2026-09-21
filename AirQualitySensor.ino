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
// For esp_netif_get_ip_info: the only reliable way to ask whether this device
// is on the network, given that Matter and not Arduino brought it up.
#include <esp_netif.h>


#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "esp_sleep.h"
// For holding the PMS in reset through deep sleep — see holdPmsOff().
#include "driver/gpio.h"
#include "driver/rtc_io.h"
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

// Readings gathered during this wake, filled in as they are taken. NAN means
// "not measured this cycle" and the field is left out of the payload rather
// than sent as a zero that would render on the dashboard as real data.
//
// KEEP THIS ABOVE THE FIRST FUNCTION DEFINITION IN THIS FILE. The Arduino
// build generates a forward declaration for every function in a .ino and
// inserts them all immediately before the first function definition
// (updateMatterIdentity, below). buildReadingJson and pushReading take a
// Reading, so their generated prototypes name this type — declare it any
// lower and the build fails with "'Reading' does not name a type", pointing
// at those functions rather than at this struct. test/run.sh checks the
// ordering so a tidy-up cannot quietly reintroduce it.
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

// Last reading each sensor actually produced, and how many wakes ago. A PMS
// read fails often enough that seeding Matter with the 0.0 initialisers would
// publish "perfect air" to the hub on any cycle the sensor misses — which is
// exactly the dummy data the setup-time reads exist to avoid. Carrying the last
// good value across a couple of wakes covers a transient miss without hiding a
// sensor that has actually died.
RTC_DATA_ATTR float rtcTemperature = NAN;
RTC_DATA_ATTR float rtcHumidity = NAN;
RTC_DATA_ATTR float rtcPressure = NAN;
RTC_DATA_ATTR float rtcPM1 = NAN;
RTC_DATA_ATTR float rtcPM25 = NAN;
RTC_DATA_ATTR float rtcPM10 = NAN;

// NEVER_READ means no reading has ever succeeded, so there is nothing to fall
// back on. Counted in wakes because millis() restarts every deep sleep.
static const uint32_t NEVER_READ = 9999;
static const uint32_t MAX_FALLBACK_WAKES = 2;
RTC_DATA_ATTR uint32_t rtcBmeAgeWakes = NEVER_READ;
RTC_DATA_ATTR uint32_t rtcPmsAgeWakes = NEVER_READ;

static bool canFallBack(uint32_t ageWakes) {
    return ageWakes != NEVER_READ && ageWakes <= MAX_FALLBACK_WAKES;
}

// Read the PMS, retrying a couple of times before giving up. A single miss is
// common; several in a row on a warmed-up sensor is a real fault.
static bool readPms(PMS::DATA &out, uint8_t attempts = 3) {
    for (uint8_t i = 1; i <= attempts; ++i) {
        pms.requestRead();
        if (pms.readUntil(out)) {
            if (i > 1) Serial.printf("PMS read succeeded on attempt %u\r\n", i);
            return true;
        }
        Serial.printf("PMS read attempt %u of %u failed\r\n", i, attempts);
    }
    return false;
}

// Read the BME, retrying a couple of times. Same reasoning as readPms: one
// failed forced measurement is not evidence the sensor is gone, and on a cold
// boot there is no RTC value to fall back on, so it is worth asking again
// before the cluster gets seeded with a number nothing measured.
static bool readBme(float &t, float &h, float &pr, uint8_t attempts = 3) {
    for (uint8_t i = 1; i <= attempts; ++i) {
        bme.takeForcedMeasurement();
        t = bme.readTemperature();
        h = bme.readHumidity();
        pr = bme.readPressure() / 100.0F;
        if (!isnan(t) && !isnan(h) && !isnan(pr)) {
            if (i > 1) Serial.printf("BME read succeeded on attempt %u\r\n", i);
            return true;
        }
        Serial.printf("BME read attempt %u of %u failed\r\n", i, attempts);
        delay(20);
    }
    return false;
}

// Record a successful particulate reading everywhere it is needed: the values
// to publish this wake, and the RTC copy that survives deep sleep.
static void acceptPms(float pm1, float pm25, float pm10) {
    wakeReading.pm1 = pm1;
    wakeReading.pm25 = pm25;
    wakeReading.pm10 = pm10;
    wakeReading.havePms = true;
    rtcPM1 = pm1;
    rtcPM25 = pm25;
    rtcPM10 = pm10;
    rtcPmsAgeWakes = 0;
}

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

// Has the station got an address yet?
//
// Do NOT use WiFi.status() here. The Matter stack owns this connection: it
// brings the station up through ESP-IDF directly, using credentials it holds
// in NVS, and never goes through the Arduino WiFi object. So WiFi.status()
// reports WL_STOPPED (254) the whole time the device is perfectly happily
// associated and talking to the fabric — which silently skipped every hub
// push while SmartThings kept updating over the very same connection.
//
// The network interface knows the truth whoever brought it up, so ask that.
// Sockets go through lwIP and do not care which layer connected either.
static bool stationHasIp(IPAddress *out) {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) return false;
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK) return false;
    if (ip.ip.addr == 0) return false;          // up, but no DHCP lease yet
    if (out != nullptr) *out = IPAddress(ip.ip.addr);
    return true;
}

// Wait for the Matter stack to finish reassociating after wake. It reconnects
// from NVS on its own; we only decide how long to be patient.
static bool waitForWifi(unsigned long timeoutMs) {
    unsigned long start = millis();
    IPAddress ip;
    while (!stationHasIp(&ip)) {
        if (millis() - start > timeoutMs) {
            Serial.printf("No IP after %lums - skipping hub push this cycle\r\n",
                          timeoutMs);
            return false;
        }
        delay(100);
    }
    Serial.printf("Network ready after %lums, IP %s\r\n",
                  millis() - start, ip.toString().c_str());
    return true;
}

// A single POST. Returns the HTTP status, or a negative HTTPClient error code,
// or 0 for "do not bother trying again". Logs its own outcome, since this is
// read off a serial console with no debugger attached.
static int attemptPush(const char *body) {
    HTTPClient http;
    http.setConnectTimeout(WEATHER_HUB_TIMEOUT_MS);
    http.setTimeout(WEATHER_HUB_TIMEOUT_MS);
    if (!http.begin(WEATHER_HUB_URL)) {
        Serial.println("Hub push failed: bad URL");
        return 0;   // never a real status or error code
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " WEATHER_HUB_TOKEN);

    int code = http.POST((uint8_t *)body, strlen(body));
    if (code > 0 && code != 200) {
        // The hub explains rejections in the body.
        Serial.printf("Hub push rejected: HTTP %d %s\r\n", code, http.getString().c_str());
    } else if (code <= 0) {
        Serial.printf("Hub push failed: %s\r\n", HTTPClient::errorToString(code).c_str());
    }
    http.end();
    return code;
}

// Transport failures and 5xx are what a NAS under load produces, and they pass.
// A 4xx is the hub telling us the request itself is wrong — a bad token will
// still be bad in two seconds, so repeating it only burns the wake window.
static bool worthRetrying(int code) {
    return code < 0 || code >= 500;
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

    Serial.printf("Hub push: %s\r\n", body);
    int code = 0;
    for (uint8_t attempt = 1; attempt <= WEATHER_HUB_ATTEMPTS; ++attempt) {
        code = attemptPush(body);
        if (code == 200 || !worthRetrying(code)) break;
        if (attempt < WEATHER_HUB_ATTEMPTS) {
            Serial.printf("Retrying hub push (attempt %u of %u)\r\n",
                          (unsigned)(attempt + 1), (unsigned)WEATHER_HUB_ATTEMPTS);
            delay(WEATHER_HUB_RETRY_DELAY_MS);
        }
    }

    if (code == 200) {
        rtcPushOk++;
        Serial.printf("Hub push ok (%lu ok / %lu failed over %lu wakes)\r\n",
                      rtcPushOk, rtcPushFailed, rtcWakeCount);
    } else {
        rtcPushFailed++;
    }
}

// ---------------------------------------------------------------------------
// Keeping the PMS asleep through deep sleep
// ---------------------------------------------------------------------------
// pms.sleep() works — the fan audibly stops. But it starts again the moment
// the ESP32 deep-sleeps, because the UART peripheral shuts down and the pin we
// transmit on stops being driven. A floating line at the module's RX reads as
// activity and wakes it, so the fan ran for the whole five-minute sleep: about
// 100mA continuously, and a fan rated for ~8000 hours burned at 24 hours a day
// instead of the ~4.5 the cycle intends.
//
// The fix is to keep that pin at the UART idle level (high) and latch it, so
// it holds through deep sleep instead of floating. The latch survives into the
// next wake, so it has to be released before the UART can drive the pin again.

// Stop the PMS for the duration of deep sleep.
//
// pms.sleep() is not enough: the fan audibly stops when it is sent, then
// starts again the moment the ESP32 sleeps. Parking our transmit line at idle
// did not help either, so the module is not being woken by a floating pin — it
// simply does not stay in software sleep. SET, the pin designed for this, is
// not populated in this module's cable.
//
// RST is, and it is a level rather than a command: held low the module's MCU
// stays in reset and the fan stops, for exactly as long as it is held.
// Confirmed by hand with a jumper before writing this.
//
// Left running, the fan burns ~100mA continuously and spends its ~8000 hour
// life at 24 hours a day instead of the ~4.5 this cycle intends — under a
// year rather than a decade.
//
// Only low-power pins (GPIO0-7 on the C6) can hold a level through deep sleep,
// and only via the RTC path: this chip has no equivalent for ordinary digital
// pins, gpio_deep_sleep_hold_en() being an S2/S3 thing.
static void holdPmsOff() {
    gpio_num_t rst = (gpio_num_t)PMS_RST_PIN;
    if (!rtc_gpio_is_valid_gpio(rst)) {
        // Fail loudly rather than sleeping with the fan running and no clue
        // why. Moving the wire to a non-LP pin breaks this silently otherwise.
        Serial.printf("GPIO%d is not a low-power pin - it cannot hold the PMS "
                      "in reset through deep sleep, so the fan will keep "
                      "running\r\n", (int)rst);
        return;
    }
    rtc_gpio_init(rst);
    rtc_gpio_set_direction(rst, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(rst, 0);
    rtc_gpio_hold_en(rst);
}

// Release the latch and bring the module back. Without this the pin stays
// frozen low after waking, the module never leaves reset, and every read from
// the second wake onward fails.
static void releasePmsHold() {
    gpio_num_t rst = (gpio_num_t)PMS_RST_PIN;
    if (rtc_gpio_is_valid_gpio(rst)) {
        rtc_gpio_hold_dis(rst);
        rtc_gpio_deinit(rst);   // back to the digital mux
    }
    // Out of reset, then a moment to boot before the UART talks to it. It
    // comes up in active mode; setup() sends passiveMode() again right after.
    pinMode(PMS_RST_PIN, OUTPUT);
    digitalWrite(PMS_RST_PIN, HIGH);
    delay(100);
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
    
    // Read the BME now, first thing after waking. The board has been cooling for
    // the whole deep sleep, so this is the closest it gets to ambient — the
    // radio and the PMS fan only warm it from here on. This one reading is used
    // for Matter and for the hub push; nothing re-reads it later in the cycle.
    rtcWakeCount++;
    if (rtcBmeAgeWakes != NEVER_READ) rtcBmeAgeWakes++;
    if (rtcPmsAgeWakes != NEVER_READ) rtcPmsAgeWakes++;

    float t = NAN, h = NAN, pr = NAN;
    if (readBme(t, h, pr)) {
        wakeReading.temperature = t;
        wakeReading.humidity = h;
        wakeReading.pressure = pr;
        rtcTemperature = t;
        rtcHumidity = h;
        rtcPressure = pr;
        rtcBmeAgeWakes = 0;
        Serial.printf("Wake #%lu - BME: %.2f C, %.2f %%, %.2f hPa\r\n",
                      rtcWakeCount, t, h, pr);
    } else if (canFallBack(rtcBmeAgeWakes)) {
        wakeReading.temperature = rtcTemperature;
        wakeReading.humidity = rtcHumidity;
        wakeReading.pressure = rtcPressure;
        Serial.printf("Wake #%lu - BME read failed, reusing value from %lu wake(s) ago\r\n",
                      rtcWakeCount, rtcBmeAgeWakes);
    } else {
        // Leaves wakeReading as NAN: the hub omits the fields rather than
        // receiving a fabricated number.
        Serial.printf("Wake #%lu - BME read failed and no recent value to reuse\r\n",
                      rtcWakeCount);
    }

    // Release last cycle's latches and bring the PMS out of reset.
    releasePmsHold();
    pmsSerial.begin(PMS_BAUD, SERIAL_8N1, PMS_UART_RX_PIN, PMS_UART_TX_PIN);
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
    // No second BME read here: the values taken at the top of the wake are used
    // throughout, before the radio and fan had a chance to warm the board.
    float temperature = wakeReading.temperature;
    float pressure = wakeReading.pressure;
    float humidity = wakeReading.humidity;

    Serial.println("Setup Reading PMS data");
    if (readPms(data))
    {
        Serial.println("Setup PMS data recieved");
        acceptPms(data.PM_AE_UG_1_0, data.PM_AE_UG_2_5, data.PM_AE_UG_10_0);
        Serial.printf("PM1: %.1f ppm\r\n", wakeReading.pm1);
        Serial.printf("PM2.5: %.1f ppm\r\n", wakeReading.pm25);
        Serial.printf("PM10: %.1f ppm\r\n", wakeReading.pm10);
    }
    else if (canFallBack(rtcPmsAgeWakes))
    {
        wakeReading.pm1 = rtcPM1;
        wakeReading.pm25 = rtcPM25;
        wakeReading.pm10 = rtcPM10;
        wakeReading.havePms = true;
        Serial.printf("PMS read failed, reusing values from %lu wake(s) ago\r\n",
                      rtcPmsAgeWakes);
    }
    else
    {
        Serial.println("PMS read failed and no recent values to reuse");
    }
    pms.sleep();

    // begin() creates the endpoints, so it cannot be skipped, and it needs a
    // number for each attribute. Everywhere else a reading we do not have is
    // simply not published — the loop's setters below are guarded, so a missing
    // sensor leaves its cluster untouched rather than reporting 0 C and perfect
    // air, which is worse than a gap because it looks like data.
    //
    // The one case these placeholders are still visible is a cold boot (no RTC
    // history) where the sensor fails every retry: the endpoint is created with
    // them and stays that way until a read succeeds. readBme/readPms retry to
    // make that unlikely; a sensor that is genuinely dead will show 0 C, and
    // the serial log says which one is missing.
    bool haveBme = !isnan(wakeReading.temperature);
    if (!haveBme || !wakeReading.havePms) {
        Serial.printf("Seeding Matter with partial data (BME %s, PMS %s)\r\n",
                      haveBme ? "ok" : "MISSING",
                      wakeReading.havePms ? "ok" : "MISSING");
    }
    weatherStation.begin(
        haveBme ? wakeReading.temperature : 0.0f,
        haveBme ? wakeReading.humidity : 0.0f,
        haveBme ? wakeReading.pressure : 900.0f,
        wakeReading.havePms ? wakeReading.pm1 : 0.0f,
        wakeReading.havePms ? wakeReading.pm25 : 0.0f,
        wakeReading.havePms ? wakeReading.pm10 : 0.0f);

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

        // Publish the reading taken at the top of the wake. These setters run
        // after Matter.begin(), which is what actually pushes the values out to
        // the fabric — begin()'s earlier call only seeds the cluster config.
        // The board is warmer now than when the values were taken, which is
        // exactly why they are not re-read here.
        //
        // NAN means the BME gave nothing this wake and there was no recent
        // value to reuse. The cluster stores temperature as a scaled int16, so
        // a NAN would arrive as a garbage number and propagate to SmartThings
        // as a real reading. Publish nothing instead and let the cluster keep
        // what it last held. The three are read and stored as a group, so one
        // test covers all of them.
        if (!isnan(wakeReading.temperature))
        {
            weatherStation.setTemperature(wakeReading.temperature);
            weatherStation.setPressure(wakeReading.pressure);
            weatherStation.setHumidity(wakeReading.humidity);
            Serial.printf("Published to Matter: %.2f C, %.2f hPa, %.2f %%\r\n",
                          wakeReading.temperature, wakeReading.pressure, wakeReading.humidity);
        }
        else
        {
            Serial.println("No BME reading this wake - Matter keeps its previous value");
        }
    }
        
    if (currentMillis - previousMillis >= (unsigned long)PMS_WARMUP_SECONDS * 1000UL) 
    {   
        previousMillis = currentMillis;
        
        Serial.println(".");
        Serial.println("Reading PMS data");
        if (readPms(data))
        {
            Serial.println("PMS data recieved");
            acceptPms(data.PM_AE_UG_1_0, data.PM_AE_UG_2_5, data.PM_AE_UG_10_0);
            Serial.printf("PM1: %.1f ppm\r\n", wakeReading.pm1);
            Serial.printf("PM2.5: %.1f ppm\r\n", wakeReading.pm25);
            Serial.printf("PM10: %.1f ppm\r\n", wakeReading.pm10);
            weatherStation.setPM1(wakeReading.pm1);
            weatherStation.setPM2_5(wakeReading.pm25);
            weatherStation.setPM10(wakeReading.pm10);
            // setPM10 was the last setter, so the air-quality enum is current
            // by the time the hub push reads it.
            Serial.printf("PM concentration: set\r\n");
        }
        else if (wakeReading.havePms)
        {
            // Nothing new, but this wake does have a particulate value —
            // measured in setup() or carried over from a recent wake. Publish
            // that, so the cluster and the hub agree on what this wake reported.
            weatherStation.setPM1(wakeReading.pm1);
            weatherStation.setPM2_5(wakeReading.pm25);
            weatherStation.setPM10(wakeReading.pm10);
            Serial.println("PMS read failed - published the value taken earlier this wake");
        }
        else
        {
            // Nothing measured and nothing recent enough to reuse. The zeros
            // the cluster was seeded with would read as pristine air, so leave
            // the cluster alone and let the gap show.
            Serial.println("PMS read failed and no value to publish - Matter keeps its previous");
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
        // Last thing before the lights go out: hold the PMS in reset so its
        // fan stays stopped for the whole sleep.
        holdPmsOff();
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