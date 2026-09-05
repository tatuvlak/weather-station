#include "MatterWeatherStation.h"
#include "AQI.h"

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

MatterWeatherStation::MatterWeatherStation() {}
MatterWeatherStation::~MatterWeatherStation() { started = false; }

bool MatterWeatherStation::begin(float temp, float hum, float pres, float pm1, float pm25, float pm10)
{
    ArduinoMatter::_init();

    if (getEndPointId() != 0) return false;

    // 1. Create the base endpoint as a PRESSURE SENSOR (0x0305)
    // We create this FIRST to ensure the Pressure Cluster (0x0403) secures its memory slot.
    esp_matter::endpoint::pressure_sensor::config_t pres_config;
    pres_config.pressure_measurement.pressure_measured_value = (int16_t)(pres * 10);
    pres_config.pressure_measurement.pressure_min_measured_value = nullptr;
    pres_config.pressure_measurement.pressure_max_measured_value = nullptr;

    endpoint_t *endpoint = esp_matter::endpoint::pressure_sensor::create(node::get(), &pres_config, ENDPOINT_FLAG_NONE, (void *)this);
    
    if (endpoint == nullptr) {
        log_e("CRITICAL: Failed to create Pressure Endpoint!");
        return false;
    }
    setEndPointId(esp_matter::endpoint::get_id(endpoint));
    log_i("Endpoint created with ID: %d", getEndPointId());

    // 2. Add Composite Device Types (Crucial for SmartThings)
    // Tell the hub this is ALSO an Air Quality Sensor (0x002C) and Temp Sensor (0x0302)
    esp_matter::endpoint::add_device_type(endpoint, 0x002C, 1); // Air Quality
    esp_matter::endpoint::add_device_type(endpoint, 0x0302, 1); // Temperature
    esp_matter::endpoint::add_device_type(endpoint, 0x0307, 1); // Humidity

    // 3. Add Temperature Cluster
    esp_matter::cluster::temperature_measurement::config_t t_cfg;
    t_cfg.measured_value = (int16_t)(temp * 100);
    t_cfg.min_measured_value = (int16_t)-4000;
    t_cfg.max_measured_value = (int16_t)8500;
    if (!esp_matter::cluster::temperature_measurement::create(endpoint, &t_cfg, CLUSTER_FLAG_SERVER)) {
        log_e("Failed to create Temperature Cluster (Limit Reached?)");
    }

    // 4. Add Humidity Cluster
    esp_matter::cluster::relative_humidity_measurement::config_t h_cfg;
    h_cfg.measured_value = (uint16_t)(hum * 100);
    h_cfg.min_measured_value = (uint16_t)0;
    h_cfg.max_measured_value = (uint16_t)10000;
    if (!esp_matter::cluster::relative_humidity_measurement::create(endpoint, &h_cfg, CLUSTER_FLAG_SERVER)) {
        log_e("Failed to create Humidity Cluster (Limit Reached?)");
    }

    // 5. Add Air Quality Cluster (Mandatory for PM sensors)
    esp_matter::cluster::air_quality::config_t aq_cfg;
    if (!esp_matter::cluster::air_quality::create(endpoint, &aq_cfg, CLUSTER_FLAG_SERVER)) {
        log_e("Failed to create Air Quality Cluster (Limit Reached?)");
    }

    // 6. Add Dust Clusters
    uint8_t flags = ATTRIBUTE_FLAG_NULLABLE;
    esp_matter_attr_val_t feature_numeric = esp_matter_uint32(1);

    auto add_dust = [&](uint32_t cluster_id, float val, const char* name) {
        cluster_t *c = esp_matter::cluster::create(endpoint, cluster_id, CLUSTER_FLAG_SERVER);
        if (c) {
            esp_matter::attribute::create(c, 0x0000, flags, esp_matter_nullable_float(val));
            esp_matter::attribute::create(c, 0xFFFC, 0, feature_numeric);
        } else {
            log_e("Failed to create %s Cluster! (Limit Reached)", name);
        }
    };

    add_dust(0x042A, pm25, "PM2.5"); // Priority 1
    add_dust(0x042D, pm10, "PM10");  // Priority 2
    add_dust(0x042C, pm1, "PM1");    // Priority 3 (Least critical)

    // 7. Manually set Pressure Bounds (Required for UI)
    esp_matter_attr_val_t minP = esp_matter_int16(8000); 
    esp_matter_attr_val_t maxP = esp_matter_int16(12000); 
    attribute::update(getEndPointId(), 0x0403, 0x0001, &minP);
    attribute::update(getEndPointId(), 0x0403, 0x0002, &maxP);

    started = true;
    
    // Initialize State
    setTemperature(temp);
    setHumidity(hum);
    setPressure(pres);
    valPM1 = pm1;
    valPM25 = pm25;
    valPM10 = pm10;
    setPM1(pm1);
    setPM2_5(pm25);
    setPM10(pm10);

    return true;
}

// Setters
bool MatterWeatherStation::setTemperature(float temp) {
    int16_t val = (int16_t)(temp * 100);
    if (val == rawTemp && started) return true;
    rawTemp = val;
    esp_matter_attr_val_t attr = esp_matter_nullable_int16(val);
    return attribute::update(getEndPointId(), 0x0402, 0x0000, &attr) == ESP_OK;
}

bool MatterWeatherStation::setHumidity(float hum) {
    uint16_t val = (uint16_t)(hum * 100);
    if (val == rawHum && started) return true;
    rawHum = val;
    esp_matter_attr_val_t attr = esp_matter_nullable_uint16(val);
    return attribute::update(getEndPointId(), 0x0405, 0x0000, &attr) == ESP_OK;
}

bool MatterWeatherStation::setPressure(float pres) {
    int16_t val = (int16_t)(pres * 10); 
    if (val == rawPres && started) return true;
    rawPres = val;
    esp_matter_attr_val_t attr = esp_matter_nullable_int16(val);
    return attribute::update(getEndPointId(), 0x0403, 0x0000, &attr) == ESP_OK;
}

bool MatterWeatherStation::setPM1(float pm1) {
    valPM1 = pm1;
    esp_matter_attr_val_t attr = esp_matter_nullable_float(pm1);
    updateAQIStatus();
    return attribute::update(getEndPointId(), 0x042C, 0x0000, &attr) == ESP_OK;
}

bool MatterWeatherStation::setPM2_5(float pm25) {
    valPM25 = pm25;
    esp_matter_attr_val_t attr = esp_matter_nullable_float(pm25);
    updateAQIStatus();
    return attribute::update(getEndPointId(), 0x042A, 0x0000, &attr) == ESP_OK;
}

bool MatterWeatherStation::setPM10(float pm10) {
    valPM10 = pm10;
    esp_matter_attr_val_t attr = esp_matter_nullable_float(pm10);
    updateAQIStatus();
    return attribute::update(getEndPointId(), 0x042D, 0x0000, &attr) == ESP_OK;
}

void MatterWeatherStation::updateAQIStatus() {
    int aqi = AQI::calculate(valPM1, valPM25, valPM10);
    uint8_t enumaq = 0; 
    if(aqi < 0)
        enumaq = 0; // Unknown
    else if (aqi <= 50)
        enumaq = 1; // Good
    else if (aqi <= 100)
        enumaq = 2; // Fair
    else if (aqi <= 150)
        enumaq = 3; // Moderate
    else if (aqi <= 200)
        enumaq = 4; // Poor
    else if (aqi <= 300)
        enumaq = 5; // Very Poor
    else
        enumaq = 6; // Extremely Poor

    valAQI = enumaq;

    esp_matter_attr_val_t attr = esp_matter_enum8(enumaq);
    // FIX: Use getEndPointId() instead of aq_endpoint_id
    attribute::update(getEndPointId(), 0x005B, 0x0000, &attr);
}

bool MatterWeatherStation::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
    return true; 
}