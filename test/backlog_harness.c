/* Stubs so the backlog lifted from the sketch can run on a PC.
 *
 * The ring buffer is the part of this firmware with the least supervision: it
 * only does anything during a hub outage, on a sensor mounted outside, and the
 * symptom of getting the arithmetic wrong is a hole in the history that nobody
 * notices for weeks. So the real enqueue and flush code is compiled here and
 * driven through the cases that matter.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <cstdint>
#include <cstdarg>

#define WEATHER_BACKLOG_SLOTS 4            /* small, so the wrap is easy to drive */
#define WEATHER_BACKLOG_FLUSH_PER_WAKE 2
#define WEATHER_HUB_ATTEMPTS 2
#define WEATHER_HUB_RETRY_DELAY_MS 0

#define RTC_DATA_ATTR /* nothing: plain globals on a PC */

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

/* PendingReading and the ring indices come straight from the sketch, so this
 * cannot test a copy that has drifted from the firmware. */
#include "backlog_state.inc"

/* Quiet by default; -v turns the firmware's own narration back on. */
static bool g_verbose = false;
struct SerialStub {
    void printf(const char *fmt, ...) {
        if (!g_verbose) return;
        va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    }
    void println(const char *s) { if (g_verbose) printf("%s\n", s); }
} Serial;

static unsigned int g_now = 0;
static unsigned int uptimeNow() { return g_now; }
static void delay(unsigned long) {}

/* The hub, as far as the backlog is concerned: up or down, and a log of what
 * it was sent so the test can check order and ages. */
static bool g_hubUp = true;
static char g_sent[64][256];
static int g_sentCount = 0;
static int attemptPush(const char *body) {
    if (!g_hubUp) return -1;
    if (g_sentCount < 64) snprintf(g_sent[g_sentCount], 256, "%s", body);
    g_sentCount++;
    return 200;
}
static bool worthRetrying(int code) { return code < 0 || code >= 500; }

#include "extracted.inc"   /* appendField + buildReadingJson */
#include "backlog.inc"     /* enqueueBacklog + postWithRetries + flushBacklog */

static int failures = 0;
static void check(const char *label, bool ok, const char *detail) {
    printf("  %s %s%s%s\n", ok ? "ok " : "BAD", label,
           detail && *detail ? " - " : "", detail ? detail : "");
    if (!ok) failures++;
}
static void checkInt(const char *label, long got, long want) {
    char d[64]; snprintf(d, sizeof d, "got %ld, want %ld", got, want);
    check(label, got == want, got == want ? "" : d);
}

static Reading mk(float temp, signed char aqi) {
    Reading r; r.temperature = temp; r.aqi = aqi; return r;
}
static void reset() {
    rtcBacklogHead = 0; rtcBacklogCount = 0; rtcBacklogDropped = 0;
    g_sentCount = 0; g_hubUp = true; g_now = 0;
    for (int i = 0; i < WEATHER_BACKLOG_SLOTS; i++) rtcBacklog[i] = PendingReading();
}

int main(int argc, char **argv) {
    g_verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);

    printf("\nholding readings while the hub is down\n");
    reset();
    for (int i = 1; i <= 3; i++) { g_now = i * 300; enqueueBacklog(mk(i, 1), g_now); }
    checkInt("three readings held", rtcBacklogCount, 3);
    checkInt("nothing dropped yet", rtcBacklogDropped, 0);

    printf("\ndraining oldest first, and only so many per wake\n");
    g_now = 1200;
    flushBacklog();
    checkInt("sent this wake", g_sentCount, WEATHER_BACKLOG_FLUSH_PER_WAKE);
    checkInt("the rest stay held", rtcBacklogCount, 1);
    /* Oldest first: reading 1 (taken at 300) before reading 2 (at 600). */
    check("oldest went first", strstr(g_sent[0], "\"temperature_c\":1.00") != NULL, g_sent[0]);
    check("then the next oldest", strstr(g_sent[1], "\"temperature_c\":2.00") != NULL, g_sent[1]);
    /* Age must be elapsed time, not the moment it was queued. */
    check("first carried age 900", strstr(g_sent[0], "\"age_s\":900") != NULL, g_sent[0]);
    check("second carried age 600", strstr(g_sent[1], "\"age_s\":600") != NULL, g_sent[1]);

    printf("\na full ring drops the OLDEST, not the newest\n");
    reset();
    for (int i = 1; i <= 6; i++) { g_now = i * 300; enqueueBacklog(mk(i, 1), g_now); }
    checkInt("still only holds its capacity", rtcBacklogCount, WEATHER_BACKLOG_SLOTS);
    checkInt("and counted what it threw away", rtcBacklogDropped, 2);
    g_now = 3000;
    /* Drain it fully, two wakes at a time. */
    flushBacklog(); flushBacklog();
    checkInt("everything held was sent", g_sentCount, WEATHER_BACKLOG_SLOTS);
    checkInt("ring is empty", rtcBacklogCount, 0);
    /* Readings 1 and 2 were the ones dropped, so 3 is the oldest survivor. */
    check("kept the most recent, dropped the first",
          strstr(g_sent[0], "\"temperature_c\":3.00") != NULL, g_sent[0]);
    check("and the newest is still there",
          strstr(g_sent[3], "\"temperature_c\":6.00") != NULL, g_sent[3]);

    printf("\nthe hub going down mid-drain\n");
    reset();
    for (int i = 1; i <= 3; i++) { g_now = i * 300; enqueueBacklog(mk(i, 1), g_now); }
    g_now = 1200;
    g_hubUp = false;
    flushBacklog();
    checkInt("nothing was sent", g_sentCount, 0);
    checkInt("and nothing was lost", rtcBacklogCount, 3);
    g_hubUp = true;
    flushBacklog();
    flushBacklog();
    checkInt("all three arrive once it is back", g_sentCount, 3);
    check("in the order they were taken",
          strstr(g_sent[0], "\"temperature_c\":1.00") != NULL &&
          strstr(g_sent[2], "\"temperature_c\":3.00") != NULL, g_sent[2]);

    printf("\nwrapping around the end of the ring repeatedly\n");
    /* head walks past the end of the array many times; the arithmetic has to
     * keep holding. */
    reset();
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < 3; i++) { g_now += 300; enqueueBacklog(mk(round * 10 + i, 1), g_now); }
        while (rtcBacklogCount) flushBacklog();
    }
    checkInt("ring empty after ten rounds", rtcBacklogCount, 0);
    checkInt("every reading went exactly once", g_sentCount, 30);
    checkInt("none dropped", rtcBacklogDropped, 0);

    printf("\nan empty ring is a no-op\n");
    reset();
    flushBacklog();
    checkInt("nothing sent", g_sentCount, 0);
    checkInt("count unchanged", rtcBacklogCount, 0);

    printf("\nage is never zero, which would mean \"now\" to the hub\n");
    reset();
    g_now = 300;
    enqueueBacklog(mk(7, 1), 300);          /* queued and flushed in the same second */
    flushBacklog();
    checkInt("sent", g_sentCount, 1);
    check("age_s present and at least 1", strstr(g_sent[0], "\"age_s\":1") != NULL, g_sent[0]);

    printf("\n%s\n", failures ? "FAILURES" : "all backlog checks passed");
    return failures ? 1 : 0;
}
