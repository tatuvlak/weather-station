/* PmsBench - is the PMS5003 sensing, or only talking?
 *
 * Flash this INSTEAD of AirQualitySensor.ino, watch the serial monitor at
 * 115200 for two minutes, then flash the real firmware back.
 *
 * The question it answers
 * -----------------------
 * The station reports 0.0 for PM1, PM2.5 and PM10 on every wake. That has two
 * very different causes and they look identical from the hub:
 *
 *   a) the module is fine and the firmware is mishandling it - wrong mode,
 *      reading too early, reading a frame measured before the fan was at speed
 *   b) the module talks perfectly and no longer senses - a dead laser gives
 *      well-formed, correctly checksummed frames full of zeros, with the fan
 *      still audibly running
 *
 * This sketch removes every part of (a). No deep sleep, no reset pin, no
 * passive mode, no wakeUp, no sleep, no WiFi, no Matter. The module is powered,
 * held out of reset, and left in the ACTIVE mode it boots into, streaming a
 * frame about once a second. All this does is print them.
 *
 * Reading the result
 * ------------------
 * After two minutes of continuous running:
 *
 *   non-zero values appear     -> the sensor works. The fault is in the
 *                                 station firmware, and this bench is the
 *                                 baseline to fix it against.
 *   every frame reads 0/0/0    -> the sensor is talking and not sensing.
 *                                 No firmware change can fix that; the module
 *                                 needs replacing.
 *   no frames at all           -> wiring, baud or power. Check TX/RX are not
 *                                 swapped and that RST is high.
 *
 * Breathe on the inlet, or hold a smouldering match near it, once numbers are
 * flowing. A working module reacts within seconds and climbs into the
 * hundreds. One that answers every frame with a flat zero through that is not
 * measuring anything.
 *
 * What it measured here, 2026-10-03
 * ---------------------------------
 * This module senses correctly. From a cold start, in ordinary indoor air:
 *
 *   0-30s   AE 0 / 0 / 0            nothing at all
 *   32s     AE 0 / 1 / 6            first non-zero
 *   35s     AE 1 / 10 / 15          climbing fast, still badly low
 *   45s     AE 24 / 35 / 42         within about 10% of the plateau
 *   58s+    AE 27 / 39 / 48         plateau, steady for the next six minutes
 *
 * So the module needs roughly a minute, and anything read in the first 30
 * seconds is a zero that looks exactly like clean air. PMS_WARMUP_SECONDS is
 * set from these numbers.
 */

#include <HardwareSerial.h>
#include "PMS.h"

// Same pins as config.example.h. Change here if the wiring changed.
#define PMS_UART_RX_PIN 4    // ESP32 receives here  <- module TX
#define PMS_UART_TX_PIN 5    // ESP32 transmits here -> module RX
#define PMS_RST_PIN     3    // module RST: low holds it in reset
#define PMS_BAUD        9600

HardwareSerial pmsSerial(2);
PMS pms(pmsSerial);
PMS::DATA data;

static unsigned long startedAt = 0;
static unsigned long lastFrameAt = 0;
static unsigned long frames = 0;
static unsigned long nonZeroFrames = 0;

void setup() {
    Serial.begin(115200);
    delay(2000);                 // let the USB serial attach before printing
    Serial.println();
    Serial.println("PmsBench - active mode, no sleep, no reset, no mode changes");

    // The station holds this pin low through deep sleep to stop the fan. Drive
    // it high and leave it there: anything else and the module never boots.
    pinMode(PMS_RST_PIN, OUTPUT);
    digitalWrite(PMS_RST_PIN, HIGH);
    delay(1000);                 // the module's MCU needs time to come up

    pmsSerial.begin(PMS_BAUD, SERIAL_8N1, PMS_UART_RX_PIN, PMS_UART_TX_PIN);

    // Deliberately no passiveMode(), no wakeUp(), no sleep(). The module boots
    // into active mode and streams on its own; that is the simplest possible
    // state to judge the hardware in.
    Serial.println("Waiting for frames. Expect roughly one a second.");
    Serial.println("Give it two minutes, then breathe on the inlet.");
    Serial.println();
    startedAt = millis();
    lastFrameAt = startedAt;
}

void loop() {
    if (!pms.read(data)) {
        // PMS::read() consumes ONE byte per call and returns true only on the
        // byte that completes a frame. So 31 of every 32 calls return false in
        // normal operation, and this branch is the hot path, not the error
        // path. Delaying here throttled parsing to about 20 bytes a second
        // against a 32-byte-a-second stream: the driver's receive buffer
        // filled, the UART dropped the overflow, and frames went from one
        // every second to one every twenty. That looked like failing hardware
        // and was this sketch starving itself. No delay now - spin.
        //
        // Judge a quiet line by when a frame last COMPLETED, not by this
        // branch being taken.
        if (millis() - lastFrameAt > 10000UL) {
            lastFrameAt = millis();
            Serial.printf("[%6lus] no complete frame for 10s "
                          "(check TX/RX are not swapped, and RST is high)\r\n",
                          (millis() - startedAt) / 1000UL);
        }
        return;
    }
    lastFrameAt = millis();

    frames++;
    const bool anyNonZero =
        data.PM_AE_UG_1_0 || data.PM_AE_UG_2_5 || data.PM_AE_UG_10_0 ||
        data.PM_SP_UG_1_0 || data.PM_SP_UG_2_5 || data.PM_SP_UG_10_0;
    if (anyNonZero) nonZeroFrames++;

    // AE is what the station publishes; SP comes from the same frame and the
    // same optics. Both flat zero on a frame that parsed is the signature of a
    // module that is talking and not sensing.
    Serial.printf("[%6lus] frame %-4lu  AE %3u / %3u / %3u   SP %3u / %3u / %3u   %s\r\n",
                  (millis() - startedAt) / 1000UL, frames,
                  data.PM_AE_UG_1_0, data.PM_AE_UG_2_5, data.PM_AE_UG_10_0,
                  data.PM_SP_UG_1_0, data.PM_SP_UG_2_5, data.PM_SP_UG_10_0,
                  anyNonZero ? "" : "<- all zero");

    // A verdict line every 30 frames, so the answer does not have to be read
    // out of the scrollback.
    if (frames % 30 == 0) {
        Serial.printf("   ... %lu frames, %lu with a non-zero value. %s\r\n",
                      frames, nonZeroFrames,
                      nonZeroFrames == 0
                        ? "Still nothing but zeros - suspect the module."
                        : "The module senses; the fault is in the firmware.");
    }
}
