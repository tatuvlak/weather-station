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

#pragma once
#include <sdkconfig.h>
#ifdef CONFIG_ESP_MATTER_ENABLE_DATA_MODEL

#include <Matter.h>
#include <MatterEndPoint.h>

class MatterAirQualitySensor : public MatterEndPoint, ArduinoMatter
{
public:
    MatterAirQualitySensor();
    ~MatterAirQualitySensor();

    //bool begin(double CO2 = 0.00)
    bool begin(double _rawPM1=0.0, double _rawPM2_5=0.0, double _rawPM10=0.0, double _rawCO2=0.0)
    {
        return begin(static_cast<uint16_t>(_rawPM1), static_cast<uint16_t>(_rawPM2_5), static_cast<uint16_t>(_rawPM10), static_cast<uint16_t>(_rawCO2));
    }

    void end();

    bool setCO2(double CO2)
    {

        return setRawCO2(static_cast<uint16_t>(CO2));
    }


    double getCO2()
    {
        return (double)rawCO2;
    }
    
    void operator=(double ppm)
    {
        setCO2(ppm);
    }
    
    operator double()
    {
        return (double)getCO2();
    }
    
    bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);
    
    uint8_t getAirQuality()
    {
        return rawaq;
    }
    
    protected:
    bool started = false;

    uint16_t rawCO2 = 0;
    uint8_t rawaq = 0;
    uint16_t rawPM1 = 0.0f;
    uint16_t rawPM2_5 = 0.0f;
    uint16_t rawPM10 = 0.0f;


    // internal functions
    //bool begin(uint16_t _rawCO2);
    bool begin(uint16_t _rawPM1, uint16_t _rawPM25, uint16_t _rawPM10, uint16_t _rawCO2);
    bool setRawCO2(uint16_t _rawCO2);
    bool setRawPM1(uint16_t _pm1);
    bool setRawPM2_5(uint16_t _pm2_5);
    bool setRawPM10(uint16_t _pm10);
    bool calculateAQI();

public:
    // PM public setters/getters (use double for consistency with CO2 API)
    bool setPM1(double pm1)
    {
        return setRawPM1(static_cast<uint16_t>(pm1));
    }

    double getPM1()
    {
        return (double)rawPM1;
    }

    bool setPM2_5(double pm2_5)
    {
        return setRawPM2_5(static_cast<uint16_t>(pm2_5));
    }

    double getPM2_5()
    {
        return (double)rawPM2_5;
    }

    bool setPM10(double pm10)
    {
        return setRawPM10(static_cast<float>(pm10));
    }

    double getPM10()
    {
        return (double)rawPM10;
    }
   
};


#endif /* CONFIG_ESP_MATTER_ENABLE_DATA_MODEL */
