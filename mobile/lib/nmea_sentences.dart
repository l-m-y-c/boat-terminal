/// NMEA 0183 framing shared by the sentence console and the phone GNSS push.
library;

/// XOR of the characters between `$`/`!` and `*`, as two upper-case hex digits.
String nmeaChecksum(String body) {
  var cs = 0;
  for (final c in body.codeUnits) {
    cs ^= c;
  }
  return cs.toRadixString(16).toUpperCase().padLeft(2, '0');
}

/// `$` + [body] + `*HH`.
String nmeaSentence(String body) => '\$$body*${nmeaChecksum(body)}';

/// True when [line] is `$…*HH` or `!…*HH` with a matching checksum.
bool nmeaChecksumOk(String line) {
  final star = line.lastIndexOf('*');
  if (star < 1 || star + 2 >= line.length) return false;
  if (line[0] != '\$' && line[0] != '!') return false;
  final offered = int.tryParse(line.substring(star + 1, star + 3), radix: 16);
  return offered != null &&
      offered == int.parse(nmeaChecksum(line.substring(1, star)), radix: 16);
}

/// `ddmm.mmmm,N`.
String nmeaLat(double lat) => _degMin(lat, 2, lat >= 0 ? 'N' : 'S');

/// `dddmm.mmmm,E`.
String nmeaLon(double lon) => _degMin(lon, 3, lon >= 0 ? 'E' : 'W');

String _degMin(double value, int degDigits, String hemi) {
  final a = value.abs();
  var deg = a.floor();
  var minutes = double.parse(((a - deg) * 60.0).toStringAsFixed(4));
  // 59.99996' rounds to 60.0000'. Carry into the degrees instead.
  if (minutes >= 60.0) {
    deg += 1;
    minutes = 0;
  }
  return '${deg.toString().padLeft(degDigits, '0')}'
      '${minutes.toStringAsFixed(4).padLeft(7, '0')},$hemi';
}

String _hhmmss(DateTime utc) =>
    '${_two(utc.hour)}${_two(utc.minute)}${_two(utc.second)}.00';

String _ddmmyy(DateTime utc) =>
    '${_two(utc.day)}${_two(utc.month)}${_two(utc.year % 100)}';

String _two(int n) => n.toString().padLeft(2, '0');

/// `$GPRMC` from a phone fix. [sogKn] and [cogDeg] fall back to 0 when unknown.
String buildRmc({
  required DateTime utc,
  required double lat,
  required double lon,
  required double sogKn,
  required double cogDeg,
}) {
  final sog = sogKn.isFinite && sogKn >= 0 ? sogKn : 0.0;
  final cog = cogDeg.isFinite && cogDeg >= 0 ? cogDeg : 0.0;
  return nmeaSentence(
    'GPRMC,${_hhmmss(utc)},A,${nmeaLat(lat)},${nmeaLon(lon)},'
    '${sog.toStringAsFixed(1)},${cog.toStringAsFixed(1)},${_ddmmyy(utc)},,,A',
  );
}

/// `$GPGGA` from a phone fix, reported as an 8-satellite GPS fix.
String buildGga({
  required DateTime utc,
  required double lat,
  required double lon,
  required double altitudeM,
}) {
  final alt = altitudeM.isFinite ? altitudeM : 0.0;
  return nmeaSentence(
    'GPGGA,${_hhmmss(utc)},${nmeaLat(lat)},${nmeaLon(lon)},1,08,1.0,'
    '${alt.toStringAsFixed(1)},M,,,',
  );
}
