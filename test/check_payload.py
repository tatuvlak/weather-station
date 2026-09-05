"""Feed the firmware's own JSON output through the hub's validator."""
import json, subprocess, sys
import os
sys.path.insert(0, os.environ.get("HUB_PATH", "../../edge-driver-http-request/python-utility"))
import weather_store

NAN = "nan"
fails = []
def case(label, args, expect_fields=None, expect_nothing=False):
    args = list(args)[:8]
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

print("\n" + ("FAILURES: " + ", ".join(fails) if fails else "all checks passed"))
sys.exit(1 if fails else 0)
