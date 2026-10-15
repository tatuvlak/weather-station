/* Stubs so the JSON builder lifted from the sketch can run on a PC. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

/* The aqi used to be fetched from weatherStation inside the builder. It now
 * travels on the Reading, so the backlog can replay a reading without the live
 * object - which also means this harness no longer needs to stub it. */
struct Reading {
    float temperature = NAN;
    float humidity = NAN;
    float pressure = NAN;
    float pm1 = NAN;
    float pm25 = NAN;
    float pm10 = NAN;
    bool  havePms = false;
    signed char aqi = -1;
    unsigned int ageS = 0;
    signed char pmsHoldOk = -1;
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
    r.aqi         = (signed char)atoi(argv[8]);
    /* Optional: age in seconds (backfill) and the PMS hold verdict. */
    if (argc > 9)  r.ageS       = (unsigned int)atoi(argv[9]);
    if (argc > 10) r.pmsHoldOk  = (signed char)atoi(argv[10]);
    if (buildReadingJson(buf, sizeof(buf), r)) printf("%s\n", buf);
    else printf("NOTHING\n");
    return 0;
}
