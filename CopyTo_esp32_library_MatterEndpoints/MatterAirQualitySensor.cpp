// Copyright 2024 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Modifications 2025 by Blue Rubber Duck
// - Change MatterEndPoint to Matter AirQuality Sensor(CO2)
//
// This file is based on code from the Espressif Matter library:
// https://github.com/espressif/arduino-esp32/tree/master/libraries/Matter/src/MatterEndpoints

#include <sdkconfig.h>

#ifdef CONFIG_ESP_MATTER_ENABLE_DATA_MODEL

#include <Matter.h>
#include <app/server/Server.h>
#include "MatterAirQualitySensor.h"
#include "AQI.h"

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

MatterAirQualitySensor::MatterAirQualitySensor()
{
}

MatterAirQualitySensor::~MatterAirQualitySensor()
{
    end();
}

bool MatterAirQualitySensor::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    if (!started)
    {
        log_e("Matter Air Quality Sensor device has not begun.");
        return false;
    }

    log_d("Air Quality Sensor Attr update callback: endpoint: %u, cluster: %u, attribute: %u, val: %u",
          endpoint_id, cluster_id, attribute_id, val->val.u32);

    return true;
}

bool MatterAirQualitySensor::begin(uint16_t _rawPM1, uint16_t _rawPM2_5, uint16_t _rawPM10, uint16_t _rawCO2)
{
/*     ArduinoMatter::_init();

    if (getEndPointId() != 0)
    {
        log_e("Matter Air Quality Sensor with Endpoint Id %d already exists.", getEndPointId());
        return false;
    }

    air_quality_sensor::config_t air_quality_sensor_config;

    endpoint_t *endpoint = air_quality_sensor::create(node::get(), &air_quality_sensor_config, ENDPOINT_FLAG_NONE, (void *)this);

    if (endpoint == nullptr)
    {
        log_e("Failed to create Air Quality Sensor endpoint.");
        return false;
    }

    uint8_t flags = ATTRIBUTE_FLAG_NULLABLE;
    esp_matter_attr_val_t val;
    /* 
    // --- CO2 concentration measurement - not used at the moment - no hardware support ---
    cluster::carbon_dioxide_concentration_measurement::config_t co2_config;
    

    cluster_t *cluster = cluster::carbon_dioxide_concentration_measurement::create(endpoint, &co2_config, CLUSTER_FLAG_SERVER);
    if (cluster == nullptr)
    {
        log_e("Failed to create CO2 Measurement cluster.");
        return false;
    }

    uint8_t flags = ATTRIBUTE_FLAG_NULLABLE;


    attribute::create(cluster, CarbonDioxideConcentrationMeasurement::Attributes::MeasuredValue::Id, flags, esp_matter_nullable_float(static_cast<float>(_rawCO2)));
    attribute::create(cluster, CarbonDioxideConcentrationMeasurement::Attributes::MinMeasuredValue::Id, flags, esp_matter_nullable_float(0.0f));
    attribute::create(cluster, CarbonDioxideConcentrationMeasurement::Attributes::MaxMeasuredValue::Id, flags, esp_matter_nullable_float(10000.0f));
    attribute::create(cluster, CarbonDioxideConcentrationMeasurement::Attributes::MeasurementUnit::Id, flags, esp_matter_enum8(0)); // PPM


    esp_matter_attr_val_t val;
    attribute_t *feature_map_attr = attribute::get(cluster, Globals::Attributes::FeatureMap::Id);
    if (feature_map_attr)
    {
        attribute::get_val(feature_map_attr, &val);
        val.val.u32 |= 0x1; 
        attribute::set_val(feature_map_attr, &val);
    }
 tutaj dodać koniec komentarza co2

    // --- PM1 concentration measurement added (mirrors CO2 handling) ---
    cluster::pm1_concentration_measurement::config_t pm1_config;
    cluster_t *pm1_cluster = cluster::pm1_concentration_measurement::create(endpoint, &pm1_config, CLUSTER_FLAG_SERVER);
    if (pm1_cluster == nullptr)
    {
        log_e("Failed to create PM1 Measurement cluster.");
        return false;
    }

    attribute::create(pm1_cluster, Pm1ConcentrationMeasurement::Attributes::MeasuredValue::Id, flags, esp_matter_nullable_float(0.0f));
    attribute::create(pm1_cluster, Pm1ConcentrationMeasurement::Attributes::MinMeasuredValue::Id, flags, esp_matter_nullable_float(0.0f));
    attribute::create(pm1_cluster, Pm1ConcentrationMeasurement::Attributes::MaxMeasuredValue::Id, flags, esp_matter_nullable_float(10000.0f));
    attribute::create(pm1_cluster, Pm1ConcentrationMeasurement::Attributes::MeasurementUnit::Id, flags, esp_matter_enum8(4)); // µg/m3
    attribute_t *feature_map_attr_pm1 = attribute::get(pm1_cluster, Globals::Attributes::FeatureMap::Id);

    if (feature_map_attr_pm1)
    {
        attribute::get_val(feature_map_attr_pm1, &val);
        val.val.u32 |= 0x1;
        attribute::set_val(feature_map_attr_pm1, &val);
    }
    // --- end PM1 addition ---

    // --- PM2.5 concentration measurement (mirrors PM1 handling) ---
    cluster::pm25_concentration_measurement::config_t pm2_5_config;
    cluster_t *pm2_5_cluster = cluster::pm25_concentration_measurement::create(endpoint, &pm2_5_config, CLUSTER_FLAG_SERVER);
    if (pm2_5_cluster == nullptr)
    {
        log_e("Failed to create PM2.5 Measurement cluster.");
        return false;
    }

    attribute::create(pm2_5_cluster, Pm25ConcentrationMeasurement::Attributes::MeasuredValue::Id, flags, esp_matter_nullable_float(0.0f));
    attribute::create(pm2_5_cluster, Pm25ConcentrationMeasurement::Attributes::MinMeasuredValue::Id, flags, esp_matter_nullable_float(0.0f));
    attribute::create(pm2_5_cluster, Pm25ConcentrationMeasurement::Attributes::MaxMeasuredValue::Id, flags, esp_matter_nullable_float(10000.0f));
    attribute::create(pm2_5_cluster, Pm25ConcentrationMeasurement::Attributes::MeasurementUnit::Id, flags, esp_matter_enum8(4)); // µg/m3
    attribute_t *feature_map_attr_pm2_5 = attribute::get(pm2_5_cluster, Globals::Attributes::FeatureMap::Id);
    if (feature_map_attr_pm2_5)
    {
        attribute::get_val(feature_map_attr_pm2_5, &val);
        val.val.u32 |= 0x1;
        attribute::set_val(feature_map_attr_pm2_5, &val);
    }
    // --- end PM2.5 addition ---

    // --- PM10 concentration measurement (mirrors PM1 handling) ---
    cluster::pm10_concentration_measurement::config_t pm10_config;
    cluster_t *pm10_cluster = cluster::pm10_concentration_measurement::create(endpoint, &pm10_config, CLUSTER_FLAG_SERVER);
    if (pm10_cluster == nullptr)
    {
        log_e("Failed to create PM10 Measurement cluster.");
        return false;
    }

    attribute::create(pm10_cluster, Pm10ConcentrationMeasurement::Attributes::MeasuredValue::Id, flags, esp_matter_nullable_float(0.0f));
    attribute::create(pm10_cluster, Pm10ConcentrationMeasurement::Attributes::MinMeasuredValue::Id, flags, esp_matter_nullable_float(0.0f));
    attribute::create(pm10_cluster, Pm10ConcentrationMeasurement::Attributes::MaxMeasuredValue::Id, flags, esp_matter_nullable_float(10000.0f));
    attribute::create(pm10_cluster, Pm10ConcentrationMeasurement::Attributes::MeasurementUnit::Id, flags, esp_matter_enum8(4)); // µg/m3
    attribute_t *feature_map_attr_pm10 = attribute::get(pm10_cluster, Globals::Attributes::FeatureMap::Id);
    if (feature_map_attr_pm10)
    {
        attribute::get_val(feature_map_attr_pm10, &val);
        val.val.u32 |= 0x1;
        attribute::set_val(feature_map_attr_pm10, &val);
    }
    // --- end PM10 addition ---

    rawCO2 = _rawCO2;
    rawPM1 = _rawPM1;
    rawPM2_5 = rawPM2_5;
    rawPM10 = _rawPM10;
    setEndPointId(endpoint::get_id(endpoint));

    log_i("Air Quality Sensor created with endpoint_id %d", getEndPointId());

    cluster_t *custom_cluster = cluster::create(endpoint, AirQuality::Id, CLUSTER_FLAG_SERVER);
    if (custom_cluster == nullptr)
    {
        log_e("Failed to create custom AirQuality cluster.");
        return false;
    }

    cluster::air_quality::config_t aq_config;
    cluster_t *clusteraq = cluster::air_quality::create(endpoint, &aq_config, CLUSTER_FLAG_SERVER);
    
    attribute::create(clusteraq, AirQuality::Attributes::AirQuality::Id, ATTRIBUTE_FLAG_NULLABLE, esp_matter_enum8(1)); // AirQuality = 1 (z. B. Fair)
    

    started = true;
    return true; 
    //tu sie kończy moja orginalna funkcja begin */

    ArduinoMatter::_init();

    if (getEndPointId() != 0) {
        log_e("Matter Air Quality Sensor already exists.");
        return false;
    }

    // 1. Create the base Air Quality Sensor endpoint
    esp_matter::endpoint::air_quality_sensor::config_t aq_sensor_config;
    endpoint_t *endpoint = esp_matter::endpoint::air_quality_sensor::create(node::get(), &aq_sensor_config, ENDPOINT_FLAG_NONE, (void *)this);

    if (endpoint == nullptr) {
        log_e("Failed to create Air Quality Sensor endpoint.");
        return false;
    }

    // Define common flags and attribute values
    uint8_t flags = ATTRIBUTE_FLAG_NULLABLE;
    esp_matter_attr_val_t unit_ugm3 = esp_matter_enum8(4); // 4 = Micrograms per cubic meter (µg/m³)
    esp_matter_attr_val_t feature_numeric = esp_matter_uint32(1); // FeatureMap 1 = Numeric Measurement

    // 2. Add Air Quality Cluster
    esp_matter::cluster::air_quality::config_t aq_cluster_config;
    cluster_t *aq_cluster = esp_matter::cluster::air_quality::create(endpoint, &aq_cluster_config, CLUSTER_FLAG_SERVER);
    // Attribute 0 for AirQuality is the Enum (1=Good, 2=Fair, etc.)
    attribute::create(aq_cluster, AirQuality::Attributes::AirQuality::Id, flags, esp_matter_enum8(1));

    // 3. Add PM2.5 Concentration Cluster
    esp_matter::cluster::pm25_concentration_measurement::config_t pm25_config;
    cluster_t *pm25_cluster = esp_matter::cluster::pm25_concentration_measurement::create(endpoint, &pm25_config, CLUSTER_FLAG_SERVER);
    // Create MeasuredValue (Id 0), Units (Id 1), and update FeatureMap
    attribute::create(pm25_cluster, Pm25ConcentrationMeasurement::Attributes::MeasuredValue::Id, flags, esp_matter_nullable_float(0.0f));
    attribute::create(pm25_cluster, Pm25ConcentrationMeasurement::Attributes::MeasurementUnit::Id, flags, unit_ugm3);
    attribute::set_val(attribute::get(pm25_cluster, Globals::Attributes::FeatureMap::Id), &feature_numeric);

    // 4. Add PM10 Concentration Cluster
    esp_matter::cluster::pm10_concentration_measurement::config_t pm10_config;
    cluster_t *pm10_cluster = esp_matter::cluster::pm10_concentration_measurement::create(endpoint, &pm10_config, CLUSTER_FLAG_SERVER);
    attribute::create(pm10_cluster, Pm10ConcentrationMeasurement::Attributes::MeasuredValue::Id, flags, esp_matter_nullable_float(0.0f));
    attribute::create(pm10_cluster, Pm10ConcentrationMeasurement::Attributes::MeasurementUnit::Id, flags, unit_ugm3);
    attribute::set_val(attribute::get(pm10_cluster, Globals::Attributes::FeatureMap::Id), &feature_numeric);

    // 5. Add PM1 Concentration Cluster
    esp_matter::cluster::pm1_concentration_measurement::config_t pm1_config;
    cluster_t *pm1_cluster = esp_matter::cluster::pm1_concentration_measurement::create(endpoint, &pm1_config, CLUSTER_FLAG_SERVER);
    attribute::create(pm1_cluster, Pm1ConcentrationMeasurement::Attributes::MeasuredValue::Id, flags, esp_matter_nullable_float(0.0f));
    attribute::create(pm1_cluster, Pm1ConcentrationMeasurement::Attributes::MeasurementUnit::Id, flags, unit_ugm3);
    attribute::set_val(attribute::get(pm1_cluster, Globals::Attributes::FeatureMap::Id), &feature_numeric);

    // 6. Set internal variables and store Endpoint ID
    rawPM1 = _rawPM1;
    rawPM2_5 = _rawPM2_5;
    rawPM10 = _rawPM10;
    rawCO2 = _rawCO2;
    setEndPointId(esp_matter::endpoint::get_id(endpoint));

    log_i("Air Quality Sensor created with endpoint_id %d", getEndPointId());

    started = true;
    return true;
}

void MatterAirQualitySensor::end()
{
    started = false;
}

bool MatterAirQualitySensor::setRawCO2(uint16_t _rawCO2)
{
    if (!started)
    {
        log_e("Matter Air Quality Sensor device has not begun.");
        return false;
    }

    if (rawCO2 == _rawCO2)
    {
        return true;
    }

    esp_matter_attr_val_t attrVal = esp_matter_invalid(NULL);

    if (!getAttributeVal(CarbonDioxideConcentrationMeasurement::Id, CarbonDioxideConcentrationMeasurement::Attributes::MeasuredValue::Id, &attrVal))
    {
        log_e("Failed to retrieve CO2 Sensor attribute.");
        return false;
    }

    float newValue = static_cast<float>(_rawCO2);
    attrVal = esp_matter_float(newValue);

    if (!updateAttributeVal(CarbonDioxideConcentrationMeasurement::Id, CarbonDioxideConcentrationMeasurement::Attributes::MeasuredValue::Id, &attrVal))
    {
        log_e("Failed to update CO2 Sensor attribute.");
        return false;
    }

    rawCO2 = _rawCO2;
    log_v("CO2 Sensor set to %.02f PPM", newValue);

    esp_matter_attr_val_t attrValAQ = esp_matter_invalid(NULL);
    if (!getAttributeVal(AirQuality::Id, AirQuality::Attributes::AirQuality::Id, &attrValAQ))
    {
        log_e("AirQuality attribute not found");
        return false;
    }
    int enumaq;



    if(_rawCO2 == 0)
    {
        enumaq = 0; // Undefined
    }
    else if (_rawCO2 <= 800)
    {
        enumaq = 1; // Excellent
    }
    else if (_rawCO2 <= 1000)
    {
        enumaq = 2; // Good
    }
    else if (_rawCO2 <= 1500)
    {
        enumaq = 3; // Fair
    }
    else if (_rawCO2 <= 2000)
    {
        enumaq = 4; // Inferior
    }
    else
    {
        enumaq = 5; // Poor
    }

    attrValAQ = esp_matter_enum8(enumaq);
    updateAttributeVal(AirQuality::Id, AirQuality::Attributes::AirQuality::Id, &attrValAQ);

    if(rawaq == enumaq)
    {
        return false;
    }
    
    rawaq = enumaq;

    Serial.print("un dos dres");
    Serial.println(enumaq);
    Serial.println(rawaq);
    
    log_v("Air Quality Sensor set to mode %d", attrValAQ);

    return true;
}

bool MatterAirQualitySensor::calculateAQI()
{
    esp_matter_attr_val_t attrValAQ = esp_matter_invalid(NULL);
    if (!getAttributeVal(AirQuality::Id, AirQuality::Attributes::AirQuality::Id, &attrValAQ))
    {
        log_e("AirQuality attribute not found");
        return false;
    }
    int enumaq;

    int aqi = AQI::calculate( rawPM1, rawPM2_5, rawPM10);

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

    attrValAQ = esp_matter_enum8(enumaq);
    updateAttributeVal(AirQuality::Id, AirQuality::Attributes::AirQuality::Id, &attrValAQ);

    if(rawaq == enumaq)
    {
        return false;
    }
    
    rawaq = enumaq;

    Serial.print("un dos dres");
    Serial.println(enumaq);
    Serial.println(rawaq);
    
    log_v("Air Quality Sensor set to mode %d", attrValAQ);

    return true;
}

bool MatterAirQualitySensor::setRawPM1(uint16_t _pm1)
{
    if (!started)
    {
        log_e("Matter Air Quality Sensor device has not begun.");
        return false;
    }

    if (rawPM1 == _pm1)
    {
        return true;
    }

    esp_matter_attr_val_t attrVal = esp_matter_invalid(NULL);
    if (!getAttributeVal(Pm1ConcentrationMeasurement::Id, Pm1ConcentrationMeasurement::Attributes::MeasuredValue::Id, &attrVal))
    {
        log_e("Failed to retrieve PM1 attribute.");
        return false;
    }

    float newValue = static_cast<float>(_pm1);
    attrVal = esp_matter_float(newValue);
    if (!updateAttributeVal(Pm1ConcentrationMeasurement::Id, Pm1ConcentrationMeasurement::Attributes::MeasuredValue::Id, &attrVal))
    {
        log_e("Failed to update PM1 attribute.");
        return false;
    }

    rawPM1 = _pm1;
    log_v("PM1 set to %.02f µg/m3", _pm1);
    calculateAQI();
    return true;
}

bool MatterAirQualitySensor::setRawPM2_5(uint16_t _pm2_5)
{
    if (!started)
    {
        log_e("Matter Air Quality Sensor device has not begun.");
        return false;
    }

    if (rawPM2_5 == _pm2_5)
    {
        return true;
    }

    esp_matter_attr_val_t attrVal = esp_matter_invalid(NULL);
    if (!getAttributeVal(Pm25ConcentrationMeasurement::Id, Pm25ConcentrationMeasurement::Attributes::MeasuredValue::Id, &attrVal))
    {
        log_e("Failed to retrieve PM2.5 attribute.");
        return false;
    }

    float newValue = static_cast<float>(_pm2_5);
    attrVal = esp_matter_float(newValue);
    if (!updateAttributeVal(Pm25ConcentrationMeasurement::Id, Pm25ConcentrationMeasurement::Attributes::MeasuredValue::Id, &attrVal))
    {
        log_e("Failed to update PM2.5 attribute.");
        return false;
    }

    rawPM2_5 = _pm2_5;
    log_v("PM2.5 set to %.02f µg/m3", _pm2_5);
    calculateAQI();
    return true;
}

bool MatterAirQualitySensor::setRawPM10(uint16_t _pm10)
{
    if (!started)
    {
        log_e("Matter Air Quality Sensor device has not begun.");
        return false;
    }

    if (rawPM10 == _pm10)
    {
        return true;
    }

    esp_matter_attr_val_t attrVal = esp_matter_invalid(NULL);
    if (!getAttributeVal(Pm10ConcentrationMeasurement::Id, Pm10ConcentrationMeasurement::Attributes::MeasuredValue::Id, &attrVal))
    {
        log_e("Failed to retrieve PM10 attribute.");
        return false;
    }

    float newValue = static_cast<float>(_pm10);
    attrVal = esp_matter_float(newValue);
    if (!updateAttributeVal(Pm10ConcentrationMeasurement::Id, Pm10ConcentrationMeasurement::Attributes::MeasuredValue::Id, &attrVal))
    {
        log_e("Failed to update PM10 attribute.");
        return false;
    }

    rawPM10 = _pm10;
    log_v("PM10 set to %.02f µg/m3", _pm10);
    calculateAQI();
    return true;
}

#endif // CONFIG_ESP_MATTER_ENABLE_DATA_MODEL
