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

start = src.index("static void appendField")
end = src.index("// Has the station got an address yet?")
pathlib.Path("extracted.inc").write_text(src[start:end])
PY

g++ -x c++ -o harness harness.c -Wall
HUB_PATH="$HUB" python3 check_payload.py
