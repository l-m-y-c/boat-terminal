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
