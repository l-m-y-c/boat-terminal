#!/usr/bin/env python3
"""Independent NMEA 0183 / AIVDM checker for the host test.

Written from ITU-R M.1371 and the gpsd AIVDM notes, not from the firmware
code, so it catches packer mistakes instead of repeating them.

  ais_check.py decode '!AIVDM,...'     print the decoded fields
  ais_check.py check  sentences.txt    verify every line (checksum, length,
                                       AIS bit length / fields) against the
                                       expectations in the '# expect' lines
"""
import sys


def checksum_ok(line):
    if len(line) > 82 or line[0] not in "$!" or line[-3] != "*":
        return False
    cs = 0
    for c in line[1:-3]:
        cs ^= ord(c)
    return "%02X" % cs == line[-2:]


def unarm(payload, fill):
    bits = ""
    for ch in payload:
        v = ord(ch) - 48
        if v > 40:
            v -= 8
        assert 0 <= v < 64, ch
        bits += format(v, "06b")
    return bits[: len(bits) - fill] if fill else bits


def u(bits, start, width):
    return int(bits[start : start + width], 2)


def s(bits, start, width):
    v = u(bits, start, width)
    return v - (1 << width) if v & (1 << (width - 1)) else v


def text(bits, start, nchars):
    out = ""
    for i in range(nchars):
        v = u(bits, start + 6 * i, 6)
        out += chr(v + 64) if v < 32 else chr(v)
    return out.rstrip("@").rstrip()


def decode_bits(bits):
    t = u(bits, 0, 6)
    d = {"type": t, "repeat": u(bits, 6, 2), "mmsi": u(bits, 8, 30), "nbits": len(bits)}
    if t in (1, 2, 3):
        d.update(status=u(bits, 38, 4), rot=s(bits, 42, 8), sog=u(bits, 50, 10) / 10,
                 accuracy=u(bits, 60, 1), lon=s(bits, 61, 28) / 600000, lat=s(bits, 89, 27) / 600000,
                 cog=u(bits, 116, 12) / 10, heading=u(bits, 128, 9), second=u(bits, 137, 6),
                 raim=u(bits, 148, 1), radio=u(bits, 149, 19))
    elif t == 5:
        d.update(ais_version=u(bits, 38, 2), imo=u(bits, 40, 30), callsign=text(bits, 70, 7),
                 shipname=text(bits, 112, 20), shiptype=u(bits, 232, 8), to_bow=u(bits, 240, 9),
                 to_stern=u(bits, 249, 9), to_port=u(bits, 258, 6), to_starboard=u(bits, 264, 6),
                 epfd=u(bits, 270, 4), month=u(bits, 274, 4), day=u(bits, 278, 5), hour=u(bits, 283, 5),
                 minute=u(bits, 288, 6), draught=u(bits, 294, 8) / 10, destination=text(bits, 302, 20),
                 dte=u(bits, 422, 1))
    elif t == 18:
        d.update(sog=u(bits, 46, 10) / 10, accuracy=u(bits, 56, 1), lon=s(bits, 57, 28) / 600000,
                 lat=s(bits, 85, 27) / 600000, cog=u(bits, 112, 12) / 10, heading=u(bits, 124, 9),
                 second=u(bits, 133, 6), cs=u(bits, 141, 1), display=u(bits, 142, 1), dsc=u(bits, 143, 1),
                 band=u(bits, 144, 1), msg22=u(bits, 145, 1), assigned=u(bits, 146, 1), raim=u(bits, 147, 1))
    elif t == 19:
        d.update(sog=u(bits, 46, 10) / 10, lon=s(bits, 57, 28) / 600000, lat=s(bits, 85, 27) / 600000,
                 cog=u(bits, 112, 12) / 10, heading=u(bits, 124, 9), shipname=text(bits, 143, 20),
                 shiptype=u(bits, 263, 8), to_bow=u(bits, 271, 9), to_stern=u(bits, 280, 9),
                 to_port=u(bits, 289, 6), to_starboard=u(bits, 295, 6), epfd=u(bits, 301, 4))
    elif t == 24:
        part = u(bits, 38, 2)
        d["partno"] = part
        if part == 0:
            d["shipname"] = text(bits, 40, 20)
        else:
            d.update(shiptype=u(bits, 40, 8), vendorid=text(bits, 48, 7), callsign=text(bits, 90, 7),
                     to_bow=u(bits, 132, 9), to_stern=u(bits, 141, 9), to_port=u(bits, 150, 6),
                     to_starboard=u(bits, 156, 6))
    return d


EXPECT_BITS = {1: 168, 2: 168, 3: 168, 5: 424, 18: 168, 19: 312}


def decode_group(lines):
    """lines: fragments of one message, in order. Returns field dict."""
    payload = ""
    fill = 0
    total = None
    seq = None
    for i, line in enumerate(lines):
        f = line[:-3].split(",")
        assert f[0] in ("!AIVDM", "!AIVDO"), line
        assert f[4] in ("A", "B"), "channel " + line
        if total is None:
            total = int(f[1])
            seq = f[3]
        assert int(f[1]) == total and int(f[2]) == i + 1, "fragment numbering " + line
        assert f[3] == seq, "sequence id differs across fragments"
        if total > 1:
            assert seq.isdigit(), "multi-fragment needs a sequential message id"
        elif seq:
            raise AssertionError("single fragment should leave message id empty")
        if i < total - 1:
            assert f[6] == "0", "only the last fragment carries fill bits"
        payload += f[5]
        fill = int(f[6])
    bits = unarm(payload, fill)
    assert (len(bits) + fill) % 6 == 0
    d = decode_bits(bits)
    t = d["type"]
    if t in EXPECT_BITS:
        assert len(bits) == EXPECT_BITS[t], "type %d is %d bits, want %d" % (t, len(bits), EXPECT_BITS[t])
    if t == 24:
        want = 160 if d["partno"] == 0 else 168
        assert len(bits) == want, "type 24 part is %d bits, want %d" % (len(bits), want)
    d["_talker"] = lines[0][:6]
    d["_channel"] = lines[0].split(",")[4]
    return d


def close(a, b):
    if isinstance(b, float):
        return abs(float(a) - b) < 1e-4
    return str(a) == str(b)


def check_file(path):
    lines = [l.rstrip("\r\n") for l in open(path)]
    failures = 0
    checked = 0
    pending = []
    expect = None
    for line in lines:
        if not line:
            continue
        if line.startswith("# expect "):
            expect = dict(kv.split("=", 1) for kv in line[len("# expect "):].split(";"))
            continue
        if line.startswith("#"):
            continue
        try:
            assert checksum_ok(line), "bad checksum/length: " + line
            checked += 1
            if line[0] == "!":
                pending.append(line)
                total = int(line.split(",")[1])
                if len(pending) < total:
                    continue
                d = decode_group(pending)
                pending = []
                if expect:
                    for k, v in expect.items():
                        try:
                            v = float(v) if "." in v else v
                        except ValueError:
                            pass
                        assert k in d, "missing field " + k
                        assert close(d[k], v), "%s: got %r want %r" % (k, d[k], v)
                    expect = None
        except AssertionError as e:
            failures += 1
            pending = []
            print("FAIL:", e)
    print("ais_check: %d sentences, %d failures" % (checked, failures))
    return failures == 0


if __name__ == "__main__":
    if sys.argv[1] == "decode":
        print(decode_group(sys.argv[2:]))
    else:
        sys.exit(0 if check_file(sys.argv[2]) else 1)
