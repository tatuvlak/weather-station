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

// Give up quickly. The device's job is Matter; posting is a bonus and must
// never hold up the wake cycle.
#define WEATHER_HUB_TIMEOUT_MS 4000

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

// How long to run the PMS fan before reading it. The datasheet wants ~30s for
// the airflow and the laser chamber to settle.
#define PMS_WARMUP_SECONDS 30

// ---------------------------------------------------------------------------
// Wi-Fi — only for builds WITHOUT BLE commissioning
// ---------------------------------------------------------------------------
// Unused on this device: CHIPoBLE is enabled, so the credentials arrive over
// BLE during commissioning and live in NVS. These exist so the
// #if !CONFIG_ENABLE_CHIPOBLE fallback still compiles.
#define WIFI_SSID "unused-when-commissioned-over-ble"
#define WIFI_PASSWORD "unused-when-commissioned-over-ble"
