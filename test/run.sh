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
import pathlib
src = pathlib.Path("../AirQualitySensor.ino").read_text()
start = src.index("static void appendField")
end = src.index("// Wait for the Matter stack")
pathlib.Path("extracted.inc").write_text(src[start:end])
PY

g++ -x c++ -o harness harness.c -Wall
HUB_PATH="$HUB" python3 check_payload.py
