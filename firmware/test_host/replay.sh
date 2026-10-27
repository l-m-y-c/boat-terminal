#!/bin/sh
# Build (when needed) and run the capture replay: a candump log through the
# NMEA2000 library, the diagnostics and the 0183 converter on the host.
#   sh firmware/test_host/replay.sh CAPTURE.log [--sentences OUT.txt] [--json OUT.json]
#   sh firmware/test_host/replay.sh --sim 60 --write sim.log
set -e
here=$(cd "$(dirname "$0")" && pwd)
fw="$here/.."
lib=$(ls -d "$fw"/.pio/libdeps/*/NMEA2000-library/src 2>/dev/null | head -n 1)
if [ -z "$lib" ]; then
    echo "NMEA2000 library not found. Run 'make firmware' once so PlatformIO fetches it." >&2
    exit 1
fi
out="${TMPDIR:-/tmp}/boat-terminal-replay"
mkdir -p "$out"
cxx=${CXX:-c++}
bin="$out/replay"
srcs="$here/replay.cpp $fw/src/n2k_diag.cpp $fw/src/n2k_describe.cpp $fw/src/n2k_sim.cpp $fw/src/n2k_to_0183.cpp $fw/src/ais0183.cpp $fw/src/nmea.cpp"
stale=0
[ -x "$bin" ] || stale=1
for s in $srcs "$fw"/src/*.h; do
    [ "$s" -nt "$bin" ] && stale=1
done
if [ "$stale" = 1 ]; then
    # The library's timer is built without its Linux branch so it follows
    # the capture's timestamps through millis().
    $cxx -std=gnu++17 -O1 -w -U__linux__ -U__linux -Ulinux -I "$here/stub" -I "$lib" \
        -c "$lib/N2kTimer.cpp" -o "$out/N2kTimer_replay.o"
    $cxx -std=gnu++17 -O1 -w -include cstdlib -include cstring -include cstdio \
        -I "$here/stub" -I "$fw/src" -I "$lib" $srcs \
        "$lib/NMEA2000.cpp" "$lib/N2kMsg.cpp" "$lib/N2kMessages.cpp" "$lib/N2kStream.cpp" \
        "$lib/N2kGroupFunction.cpp" "$lib/N2kGroupFunctionDefaultHandlers.cpp" \
        "$out/N2kTimer_replay.o" -o "$bin"
fi
exec "$bin" "$@"
