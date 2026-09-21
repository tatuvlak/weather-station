// Copy this file to config.h and fill in your values.
//
//     cp config.example.h config.h
//
// config.h is gitignored, so an edit to the sketch can no longer accidentally
// commit the hub token. (Wi-Fi credentials are not here: the device is
// commissioned over BLE, which delivers them into NVS — nothing to configure.)

#pragma once

// ---------------------------------------------------------------------------
// Weather hub
// ---------------------------------------------------------------------------
// The QNAP service's /ingest endpoint. This is the path that replaces reading
// the sensor back out of the SmartThings cloud.
#define WEATHER_HUB_URL "http://winston:5000/ingest"

// The INGEST_TOKEN from the hub's .env. Write access only — it cannot read data
// back or command the displays, which is why it is safe on a device on a shelf.
#define WEATHER_HUB_TOKEN "paste-your-INGEST_TOKEN-here"

// How long to wait on the hub before giving up. This started at 4000ms on the
// reasoning that posting is a bonus and must never hold up the wake cycle —
// too miserly in practice. The device is awake about 70s anyway, and a NAS
// busy with a RAID scrub blew straight through 4s, losing every reading for
// the duration. Ten seconds costs nothing against a 70s wake.
#define WEATHER_HUB_TIMEOUT_MS 10000

// How many times to try posting before giving up on the cycle. Only transport
// failures and 5xx are retried: a 4xx means the hub is rejecting the request
// itself, and repeating it would just burn the wake window.
#define WEATHER_HUB_ATTEMPTS 2

// Pause between attempts. Long enough for a moment's congestion to pass,
// short enough not to matter.
#define WEATHER_HUB_RETRY_DELAY_MS 1500

// How long to wait after waking for Wi-Fi to reassociate before giving up on
// the push. The Matter stack reconnects from NVS on its own; this only decides
// how patient we are about it.
#define WEATHER_WIFI_WAIT_MS 8000

// ---------------------------------------------------------------------------
// Cycle timing
// ---------------------------------------------------------------------------
// Deep sleep between measurement cycles. The device is awake for roughly 70s
// per cycle regardless, so this sets both how fresh the data is and how hard
// the PMS fan works: at 300s the fan runs ~8% of the time, at 900s ~3%.
//
// Raise this if fan life matters more than freshness — readings much more often
// than every few minutes tell you little about weather anyway. Remember to keep
// the hub's WEATHER_STALE_AFTER above this, or every reading arrives "stale".
#define SENSOR_SLEEP_SECONDS 300

// How long to run the PMS fan before reading it, once per wake.
//
// This is the main lever on fan life, and it trades directly against reading
// quality. The module is held in reset during deep sleep, so it genuinely
// cold-starts every cycle — the datasheet's ~30s is the floor for the airflow
// and laser chamber to settle from cold, and readings taken right at it came
// back suspiciously at 0.0.
//
// At 45s with a 300s sleep the fan runs ~13% of the time: roughly 7 years out
// of a part rated for 8000 hours. 30s would be ~11 years but reads sooner;
// 60s settles further and buys nothing over the two 30s warm-ups this
// replaced. Tune from the history in the hub rather than from theory.
#define PMS_WARMUP_SECONDS 45

// How long to stay awake after publishing, before deep sleep, so the Matter
// stack can actually get the report out to the fabric. It is also the only
// window in which the BOOT button's decommission long-press can be noticed,
// since the rest of the wake is spent blocked on the PMS warm-up.
#define MATTER_SETTLE_SECONDS 10

// PMS5003 serial pins, named from the ESP32's point of view. The module's TX
// pad goes to the pin we receive on; its RX pad to the pin we transmit on.
// Pinned explicitly rather than relying on the core's Serial2 defaults, which
// are not guaranteed to stay put across core versions.
#define PMS_UART_RX_PIN 4   // ESP32 receives here  <- module TX
#define PMS_UART_TX_PIN 5   // ESP32 transmits here -> module RX

// PMS5003 RESET line. Held low, the module's MCU stays in reset and the fan
// stops; released, it boots and spins up again. This is the only working off
// switch on this build: the UART sleep command does not survive the ESP32's
// deep sleep, and SET is not populated in this module's cable.
//
// Must be a low-power pin (GPIO0-7 on the C6) so it can hold its level while
// the chip sleeps. 4 and 5 are the PMS UART, 6 and 7 are the BME, so 0-3 are
// what is left.
#define PMS_RST_PIN 3

// ---------------------------------------------------------------------------
// Wi-Fi — only for builds WITHOUT BLE commissioning
// ---------------------------------------------------------------------------
// Unused on this device: CHIPoBLE is enabled, so the credentials arrive over
// BLE during commissioning and live in NVS. These exist so the
// #if !CONFIG_ENABLE_CHIPOBLE fallback still compiles.
#define WIFI_SSID "unused-when-commissioned-over-ble"
#define WIFI_PASSWORD "unused-when-commissioned-over-ble"
