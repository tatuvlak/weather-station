#pragma once
#include <sdkconfig.h>
#ifdef CONFIG_ESP_MATTER_ENABLE_DATA_MODEL

#include <Matter.h>
#include <MatterEndPoint.h>
#include "AQI.h"

class MatterWeatherStation : public MatterEndPoint, ArduinoMatter
{
public:
    MatterWeatherStation();
    ~MatterWeatherStation();

    bool begin(float temp = 0.0, float hum = 0.0, float pres = 0.0, 
               float pm1 = 0.0, float pm25 = 0.0, float pm10 = 0.0);

    // Setters
    bool setTemperature(float temp);
    bool setHumidity(float hum);
    bool setPressure(float pres);
    bool setPM1(float pm1);
    bool setPM2_5(float pm25);
    bool setPM10(float pm10);
    bool setAirQuality(uint8_t level);

    bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

protected:
    bool started = false;
    void updateAQIStatus();

    int16_t  rawTemp = 0;
    uint16_t rawHum = 0;
    int16_t  rawPres = 0;
    float    valPM1 = 0.0f;
    float    valPM25 = 0.0f;
    float    valPM10 = 0.0f;
};

#endif