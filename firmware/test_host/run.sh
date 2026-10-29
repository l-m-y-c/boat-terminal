#!/bin/sh
# Host tests for the NMEA 2000 -> 0183 converter, AIS packer, the bench
# simulator through the library's fast-packet reassembly, and the
# maintenance book's JSON reader/writer. No board needed.
# Uses the NMEA2000 library PlatformIO already fetched (run `make firmware` once).
set -e
here=$(cd "$(dirname "$0")" && pwd)
fw="$here/.."
lib=$(ls -d "$fw"/.pio/libdeps/*/NMEA2000-library/src 2>/dev/null | head -n 1)
if [ -z "$lib" ]; then
    echo "NMEA2000 library not found. Run 'make firmware' once so PlatformIO fetches it." >&2
    exit 1
fi
out="${TMPDIR:-/tmp}/boat-terminal-host-test"
mkdir -p "$out"
cxx=${CXX:-c++}
$cxx -std=gnu++17 -O1 -Wall -Wextra -Werror -I "$fw/src" \
    "$here/test_json_lite.cpp" "$fw/src/json_lite.cpp" -o "$out/test_json_lite"
# The helm instrument decoder: apparent wind only on the wind tile.
$cxx -std=gnu++17 -O1 -Wall -Wextra -Werror -I "$here/stub" -I "$fw/src" \
    "$here/test_nmea_state.cpp" "$fw/src/nmea_state.cpp" -o "$out/test_nmea_state"
"$out/test_nmea_state"
"$out/test_json_lite"
$cxx -std=gnu++17 -O1 -Wall -Wno-unused-parameter -w \
    -include cstdlib -I "$here/stub" -I "$fw/src" -I "$lib" \
    "$here/test_n2k_0183.cpp" "$fw/src/n2k_to_0183.cpp" "$fw/src/ais0183.cpp" "$fw/src/nmea.cpp" \
    "$lib/N2kMsg.cpp" "$lib/N2kMessages.cpp" "$lib/N2kTimer.cpp" "$lib/N2kStream.cpp" \
    -o "$out/test_n2k_0183"
"$out/test_n2k_0183" "$out/sentences.txt"
python3 "$here/ais_check.py" check "$out/sentences.txt"
# Simulator frames through the library's real fast-packet reassembly.
$cxx -std=gnu++17 -O1 -Wall -Wno-unused-parameter -w \
    -include cstdlib -include cstring -I "$here/stub" -I "$fw/src" -I "$lib" \
    "$here/test_n2k_sim.cpp" "$fw/src/n2k_sim.cpp" "$fw/src/n2k_to_0183.cpp" "$fw/src/ais0183.cpp" \
    "$fw/src/nmea.cpp" "$lib/NMEA2000.cpp" "$lib/N2kMsg.cpp" "$lib/N2kMessages.cpp" "$lib/N2kTimer.cpp" \
    "$lib/N2kStream.cpp" "$lib/N2kGroupFunction.cpp" "$lib/N2kGroupFunctionDefaultHandlers.cpp" \
    -o "$out/test_n2k_sim"
"$out/test_n2k_sim"
# Bus diagnostics: hints, device names, fast-packet completion, /n2k.json.
$cxx -std=gnu++17 -O1 -Wall -Wno-unused-parameter -w \
    -include cstdlib -include cstring -I "$here/stub" -I "$fw/src" -I "$lib" \
    "$here/test_n2k_diag.cpp" "$fw/src/n2k_diag.cpp" "$fw/src/n2k_describe.cpp" "$fw/src/n2k_sim.cpp" "$fw/src/n2k_to_0183.cpp" "$lib/NMEA2000.cpp" \
    "$fw/src/ais0183.cpp" "$fw/src/nmea.cpp" "$lib/N2kMsg.cpp" "$lib/N2kMessages.cpp" "$lib/N2kTimer.cpp" \
    "$lib/N2kStream.cpp" "$lib/N2kGroupFunction.cpp" "$lib/N2kGroupFunctionDefaultHandlers.cpp" -o "$out/test_n2k_diag"
"$out/test_n2k_diag"
# The terminal as an NMEA 2000 node on a virtual bus with a plotter.
# The library's timer is built without its Linux branch so it runs on
# the test's simulated millis().
$cxx -std=gnu++17 -O1 -w -U__linux__ -U__linux -Ulinux -I "$here/stub" -I "$lib" \
    -c "$lib/N2kTimer.cpp" -o "$out/N2kTimer_sim.o"
$cxx -std=gnu++17 -O1 -Wall -Wno-unused-parameter -w \
    -include cstdlib -include cstring -include cstdio -I "$here/stub" -I "$fw/src" -I "$lib" \
    "$here/test_n2k_node.cpp" "$fw/src/n2k_node.cpp" "$lib/NMEA2000.cpp" "$lib/N2kMsg.cpp" \
    "$lib/N2kMessages.cpp" "$lib/N2kStream.cpp" "$lib/N2kGroupFunction.cpp" \
    "$lib/N2kGroupFunctionDefaultHandlers.cpp" "$out/N2kTimer_sim.o" -o "$out/test_n2k_node"
"$out/test_n2k_node"
# Network discovery: the terminal asks, the simulator's virtual devices answer.
$cxx -std=gnu++17 -O1 -w -include cstdlib -include cstring -include cstdio \
    -I "$here/stub" -I "$fw/src" -I "$lib" \
    "$here/test_n2k_network.cpp" "$fw/src/n2k_network.cpp" "$fw/src/n2k_diag.cpp" "$fw/src/n2k_describe.cpp" \
    "$fw/src/n2k_sim.cpp" "$fw/src/n2k_node.cpp" "$fw/src/n2k_to_0183.cpp" "$fw/src/ais0183.cpp" "$fw/src/nmea.cpp" \
    "$lib/NMEA2000.cpp" "$lib/N2kMsg.cpp" "$lib/N2kMessages.cpp" "$lib/N2kStream.cpp" \
    "$lib/N2kGroupFunction.cpp" "$lib/N2kGroupFunctionDefaultHandlers.cpp" "$out/N2kTimer_sim.o" \
    -o "$out/test_n2k_network"
"$out/test_n2k_network"
# A Garmin GPSMAP 740s alone on the bus (Baca Lele): the plotter's GPS must
# come out as RMC with time, date and status A, plus GGA, GLL and VTG.
python3 "$here/../../tools/garmin740s_capture.py" 20 "$out/garmin740s.log"
sh "$here/replay.sh" "$out/garmin740s.log" --sentences "$out/garmin740s.txt" > "$out/garmin740s.out"
python3 - "$out/garmin740s.txt" <<'PY'
import re, sys
lines = [l.split(None, 1)[1].strip() for l in open(sys.argv[1]) if " $" in l or " !" in l]
def ok(s):
    body, _, cs = s[1:].partition("*")
    c = 0
    for ch in body:
        c ^= ord(ch)
    return cs.upper() == "%02X" % c
rmc = [l for l in lines if l.startswith("$GPRMC")]
fails = []
if len(rmc) < 15: fails.append("RMC at 1 Hz")
if not all(ok(l) for l in lines): fails.append("checksums")
good = [r for r in rmc if re.match(r"\$GPRMC,\d{6}\.\d\d,A,\d{4}\.\d{4},N,\d{5}\.\d{4},W,\d+\.\d,\d+\.\d,\d{6},", r)]
if len(good) < len(rmc) - 1: fails.append("RMC has time, A, position, SOG, COG and date")
for t in ("$GPGGA", "$GPGLL", "$GPVTG"):
    if sum(l.startswith(t) for l in lines) < 15: fails.append(t)
print("garmin 740s replay: %d sentences, %s" % (len(lines), "ok" if not fails else "FAIL " + ", ".join(fails)))
sys.exit(1 if fails else 0)
PY
