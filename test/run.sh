#!/bin/sh
# Check that what the firmware would send is something the hub accepts.
#
#     ./test/run.sh [path-to-python-utility]
#
# Extracts the JSON builder straight out of AirQualitySensor.ino, so this tests
# the real code rather than a copy that can drift, then feeds every output
# through the hub's own validator.
set -e
cd "$(dirname "$0")"
HUB="${1:-../../edge-driver-http-request/python-utility}"

python3 - <<'PY'
import pathlib, re, sys

src = pathlib.Path("../AirQualitySensor.ino").read_text()
lines = src.splitlines()

# The Arduino build generates a forward declaration for every function in the
# .ino and inserts the whole block immediately before the first function
# definition. Any type named in a signature must be declared above that point,
# or the build fails -- reporting the function, not the type, which is a
# confusing hour to spend. The gcc run below has no prototype-insertion step,
# so it cannot catch this on its own.
struct_at = next(
    (i for i, l in enumerate(lines, 1) if re.match(r"struct\s+Reading\b", l)), None)

# A function definition at file scope: starts at column 0, has a parameter
# list, opens a brace, and is not a type declaration or control statement.
func_at = next(
    (i for i, l in enumerate(lines, 1)
     if re.match(r"[A-Za-z_].*\([^;]*\)\s*\{\s*$", l)
     and not re.match(r"(struct|class|enum|union|if|for|while|switch|else)\b", l)),
    None)

if struct_at is None or func_at is None:
    sys.exit("could not locate struct Reading or the first function definition")
if struct_at > func_at:
    sys.exit(
        "struct Reading is declared at line %d, below the first function "
        "definition at line %d.\nArduino inserts its generated prototypes there, "
        "so this will fail to compile with \"'Reading' does not name a type\"."
        % (struct_at, func_at))
print("prototype ordering")
print("  ok  struct Reading (line %d) precedes the first function (line %d)"
      % (struct_at, func_at))

# config.h is gitignored, so it does not arrive with a pull. A knob added to
# config.example.h without a fallback in the sketch therefore breaks the build
# for whoever has a real config.h - and does it with an error naming the line
# that uses the macro, not the file that is missing it. Twice now.
#
# So every tuning knob must have an #ifndef default in the sketch. The ones
# listed here are required on purpose: a silently wrong URL, token or pin is
# worse than a failed build.
MUST_BE_PROVIDED = {
    "WEATHER_HUB_URL", "WEATHER_HUB_TOKEN",
    "WIFI_SSID", "WIFI_PASSWORD",
    "PMS_UART_RX_PIN", "PMS_UART_TX_PIN", "PMS_RST_PIN",
}
example = pathlib.Path("../config.example.h").read_text()
knobs = set(re.findall(r"^#define\s+(\w+)", example, re.M)) - MUST_BE_PROVIDED
missing = sorted(k for k in knobs if ("#ifndef %s" % k) not in src)
if missing:
    sys.exit(
        "these config knobs have no #ifndef default in the sketch, so a config.h\n"
        "that predates them will not compile:\n  " + "\n  ".join(missing))
print("config defaults")
print("  ok  all %d tuning knob(s) have a fallback; %d required by design"
      % (len(knobs), len(MUST_BE_PROVIDED)))

start = src.index("static void appendField")
end = src.index("// Has the station got an address yet?")
pathlib.Path("extracted.inc").write_text(src[start:end])

# The backlog, lifted the same way. It only runs during a hub outage, on a
# sensor nobody is watching, so a ring-arithmetic bug would show up as a hole
# in the history weeks later. backlog_harness.c drives the real code.
start = src.index("struct PendingReading {")
end = src.index("static uint32_t uptimeNow() {")
pathlib.Path("backlog_state.inc").write_text(src[start:end])

start = src.index("// Keep a reading the hub would not take.")
end = src.index("static void pushReading(")
pathlib.Path("backlog.inc").write_text(src[start:end])
PY

g++ -x c++ -o harness harness.c -Wall
HUB_PATH="$HUB" python3 check_payload.py

echo
g++ -x c++ -o backlog_harness backlog_harness.c -Wall -include cstdarg
./backlog_harness
