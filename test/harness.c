/* Stubs so the JSON builder lifted from the sketch can run on a PC. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

static unsigned char g_aqi = 0;
struct { unsigned char getAirQualityEnum(void) const { return g_aqi; } } weatherStation;

struct Reading {
    float temperature = NAN;
    float humidity = NAN;
    float pressure = NAN;
    float pm1 = NAN;
    float pm25 = NAN;
    float pm10 = NAN;
    bool  havePms = false;
};

#include "extracted.inc"

static float arg(const char *s) { return strcmp(s, "nan") == 0 ? NAN : atof(s); }

int main(int argc, char **argv) {
    char buf[224];
    Reading r;
    r.temperature = arg(argv[1]);
    r.humidity    = arg(argv[2]);
    r.pressure    = arg(argv[3]);
    r.havePms     = atoi(argv[4]);
    r.pm1         = arg(argv[5]);
    r.pm25        = arg(argv[6]);
    r.pm10        = arg(argv[7]);
    g_aqi         = atoi(argv[8]);
    if (buildReadingJson(buf, sizeof(buf), r)) printf("%s\n", buf);
    else printf("NOTHING\n");
    return 0;
}
