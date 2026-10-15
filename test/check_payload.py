"""Feed the firmware's own JSON output through the hub's validator."""
import json, subprocess, sys
import os
sys.path.insert(0, os.environ.get("HUB_PATH", "../../edge-driver-http-request/python-utility"))
import weather_store

NAN = "nan"
fails = []
def case(label, args, expect_fields=None, expect_nothing=False):
    # temp, humidity, pressure, havePms, pm1, pm25, pm10, aqi, [age_s], [hold]
    args = list(args)[:10]
    out = subprocess.run(["./harness", *[str(a) for a in args]],
                         capture_output=True, text=True).stdout.strip()
    if expect_nothing:
        ok = out == "NOTHING"
        print(f"  {'ok ' if ok else 'BAD'} {label} — {out}")
        if not ok: fails.append(label)
        return
    try:
        parsed = json.loads(out)
    except Exception as e:
        print(f"  BAD {label} — invalid JSON: {out!r} ({e})"); fails.append(label); return
    try:
        weather_store.validate(parsed)
    except weather_store.ValidationError as e:
        print(f"  BAD {label} — hub rejected: {e} | {out}"); fails.append(label); return
    if expect_fields is not None and sorted(parsed) != sorted(expect_fields):
        print(f"  BAD {label} — fields {sorted(parsed)} != {sorted(expect_fields)}"); fails.append(label); return
    print(f"  ok  {label} — {out}")

ALL = ["temperature_c","humidity_pct","pressure_hpa","pm1","pm25","pm10","aqi"]

print("\na complete reading")
case("all seven fields, accepted by the hub", [21.42, 48.2, 1013.25, 1, 3.1, 7.4, 9.0, 1, 0], ALL)

print("\npartial readings")
case("PMS never read — BME fields only",
     [21.4, 48.2, 1013.2, 0, 0, 0, 0, 0, 0], ["temperature_c","humidity_pct","pressure_hpa"])
case("BME absent (NaN) — PM only",
     [NAN, NAN, NAN, 1, 3.1, 7.4, 9.0, 1, 0], ["pm1","pm25","pm10","aqi"])

print("\nnothing measured")
case("no sensors at all — nothing sent", [NAN, NAN, NAN, 0, 0, 0, 0, 0, 0], expect_nothing=True)

print("\nedge values the hub must still accept")
case("sub-zero temperature", [-15.75, 92.0, 985.4, 1, 0, 0, 0, 1, 0], ALL)
case("zero PM and unknown AQI", [0.0, 0.0, 800.0, 1, 0, 0, 0, 0, 0], ALL)
case("high pollution, worst AQI", [30.0, 20.0, 1013.0, 1, 400.5, 900.25, 1999.9, 6, 0], ALL)

print("\nmetadata that rides along with a reading")
# Neither of these is a measurement. age_s is how the station says "this one is
# old" when it drains readings held through a hub outage; pms_hold_ok is its
# report on whether the sensor's reset line worked. Both must be acceptable to
# the hub, and both must be omitted when there is nothing to say.
case("hold verified", [21.4, 48.2, 1013.2, 1, 3.1, 7.4, 9.0, 1, 0, 1],
     ALL + ["pms_hold_ok"])
case("hold failed", [21.4, 48.2, 1013.2, 1, 3.1, 7.4, 9.0, 1, 0, 0],
     ALL + ["pms_hold_ok"])
case("hold not checked yet — field omitted", [21.4, 48.2, 1013.2, 1, 3.1, 7.4, 9.0, 1, 0, -1], ALL)
case("a backfilled reading carries its age", [21.4, 48.2, 1013.2, 1, 3.1, 7.4, 9.0, 1, 1800, 1],
     ALL + ["age_s", "pms_hold_ok"])
case("age 0 means now, so it is omitted", [21.4, 48.2, 1013.2, 1, 3.1, 7.4, 9.0, 1, 0, -1], ALL)
case("a backfilled BME-only reading", [21.4, 48.2, 1013.2, 0, 0, 0, 0, 0, 600, 1],
     ["temperature_c", "humidity_pct", "pressure_hpa", "age_s", "pms_hold_ok"])

# Metadata alone is not a reading: the builder must send nothing rather than a
# body the hub would reject for having no numbers in it.
case("metadata with nothing measured — still nothing sent",
     [NAN, NAN, NAN, 0, 0, 0, 0, 0, 1800, 0], expect_nothing=True)

print("\nthe age the hub will file a backfilled reading under")
# The whole point of age_s: a reading held for half an hour must land in the
# history half an hour back, not at the moment the NAS answered again.
import time
out = subprocess.run(["./harness", "21.4", "48.2", "1013.2", "1", "3.1", "7.4", "9.0", "1", "1800", "1"],
                     capture_output=True, text=True).stdout.strip()
parsed = json.loads(out)
age = weather_store.backfill_age(parsed)
ok = age == 1800.0
print(f"  {'ok ' if ok else 'BAD'} the hub reads it back as 1800s — {age}")
if not ok: fails.append("age round-trip")

reading = weather_store.validate(parsed)
ok = reading.get("pms_hold_ok") == 1 and "age_s" not in reading
print(f"  {'ok ' if ok else 'BAD'} the flag is stored and age_s is not a column — {reading}")
if not ok: fails.append("flag round-trip")

print("\n" + ("FAILURES: " + ", ".join(fails) if fails else "all checks passed"))
sys.exit(1 if fails else 0)
