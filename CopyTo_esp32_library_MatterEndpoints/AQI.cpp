#include "AQI.h"


// PM2.5 breakpoints (also used for PM1 approximation)
static const AQIBreakpoint pm25Table[] = {
    {0.0, 12.0, 0, 50},
    {12.1, 35.4, 51, 100},
    {35.5, 55.4, 101, 150},
    {55.5, 150.4, 151, 200},
    {150.5, 250.4, 201, 300},
    {250.5, 500.4, 301, 500}
};


// PM10 breakpoints
static const AQIBreakpoint pm10Table[] = {
    {0, 54, 0, 50},
    {55, 154, 51, 100},
    {155, 254, 101, 150},
    {255, 354, 151, 200},
    {355, 424, 201, 300},
    {425, 604, 301, 500}
};


float AQI::subIndex(float concentration, const AQIBreakpoint *table, uint8_t size) {
    for (uint8_t i = 0; i < size; i++) {
        if (concentration >= table[i].cLow && concentration <= table[i].cHigh) {
            return ((float)(table[i].aqiHigh - table[i].aqiLow) /
            (table[i].cHigh - table[i].cLow)) *
            (concentration - table[i].cLow) + table[i].aqiLow;
        }
    }
    return -1.0;
}


int AQI::calculate(float pm1, float pm25, float pm10) {
    float aqi1 = subIndex(pm1, pm25Table, 6);
    float aqi25 = subIndex(pm25, pm25Table, 6);
    float aqi10 = subIndex(pm10, pm10Table, 6);


    float maxAQI = aqi1;
    if (aqi25 > maxAQI) maxAQI = aqi25;
    if (aqi10 > maxAQI) maxAQI = aqi10;


    if (maxAQI < 0) return -1;
    if (maxAQI > 500) maxAQI = 500;


    return (int)(maxAQI + 0.5);
}


const char* AQI::category(int aqi) {
    if (aqi <= 50) return "Good";
    if (aqi <= 100) return "Moderate";
    if (aqi <= 150) return "Unhealthy for Sensitive Groups";
    if (aqi <= 200) return "Unhealthy";
    if (aqi <= 300) return "Very Unhealthy";
    return "Hazardous";
}