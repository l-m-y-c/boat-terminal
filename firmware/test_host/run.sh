#!/bin/sh
# Host tests for the NMEA 2000 -> 0183 converter, AIS packer, and the
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
"$out/test_json_lite"
$cxx -std=gnu++17 -O1 -Wall -Wno-unused-parameter -w \
    -include cstdlib -I "$here/stub" -I "$fw/src" -I "$lib" \
    "$here/test_n2k_0183.cpp" "$fw/src/n2k_to_0183.cpp" "$fw/src/ais0183.cpp" "$fw/src/nmea.cpp" \
    "$lib/N2kMsg.cpp" "$lib/N2kMessages.cpp" "$lib/N2kTimer.cpp" "$lib/N2kStream.cpp" \
    -o "$out/test_n2k_0183"
"$out/test_n2k_0183" "$out/sentences.txt"
python3 "$here/ais_check.py" check "$out/sentences.txt"
