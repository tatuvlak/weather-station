// ===== AQI.h =====
#ifndef AQI_H
#define AQI_H

#include <Matter.h>

typedef struct {
float cLow;
float cHigh;
int aqiLow;
int aqiHigh;
} AQIBreakpoint;


class AQI {
public:
static int calculate(float pm1, float pm25, float pm10);
static const char* category(int aqi);


private:
static float subIndex(float concentration, const AQIBreakpoint *table, uint8_t size);
};


#endif