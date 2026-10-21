import 'dart:typed_data';

import 'protocol.dart';

/// The `lmyc://pair?v=1&boat=…&tid=…&ble=…&oob=…` link in the terminal QR.
class PairingLink {
  const PairingLink({
    required this.version,
    required this.boatId,
    required this.terminalId,
    required this.bleName,
    required this.oob,
  });

  /// The only link layout this app understands.
  static const supportedVersion = '1';

  final String version;
  final String? boatId;
  final String? terminalId;
  final String? bleName;

  /// 32 hex characters, or null when the link carries none.
  final String? oob;

  /// Parses a pairing link. Returns null for anything that is not
  /// `lmyc://pair` at [supportedVersion], and drops an `oob` that is not
  /// 32 hex characters so it is never written to the terminal.
  static PairingLink? parse(Uri uri) {
    if (uri.scheme != 'lmyc' || uri.host != 'pair') {
      return null;
    }
    final params = uri.queryParameters;
    if (params['v'] != supportedVersion) {
      return null;
    }
    final oob = params['oob'];
    return PairingLink(
      version: supportedVersion,
      boatId: _nonEmpty(params['boat']),
      terminalId: _nonEmpty(params['tid']),
      bleName: _nonEmpty(params['ble']),
      oob: (oob != null && oobBytesFromHex(oob) != null) ? oob : null,
    );
  }

  /// Why [parse] refused [uri], for the status line.
  static String rejection(Uri? uri) {
    if (uri == null || uri.scheme != 'lmyc') {
      return 'Not an lmyc:// link.';
    }
    if (uri.host != 'pair') {
      return 'Not a pairing link (lmyc://pair).';
    }
    return 'This terminal uses pairing link v${uri.queryParameters['v'] ?? '?'}. '
        'Update the app.';
  }

  static String? _nonEmpty(String? s) => (s == null || s.isEmpty) ? null : s;
}

/// QR `oob=` (32 hex chars) to the 16 raw bytes the session characteristic expects.
Uint8List? oobBytesFromHex(String hex) {
  final cleaned = hex.trim();
  if (cleaned.length != 32) return null;
  final out = Uint8List(16);
  for (var i = 0; i < 16; i++) {
    final byte = int.tryParse(cleaned.substring(i * 2, i * 2 + 2), radix: 16);
    if (byte == null) return null;
    out[i] = byte;
  }
  return out;
}

/// Session replies are ASCII (OK/FAIL/WAIT/ERR). Stop at NUL — NimBLE may
/// ship a fixed-size buffer (e.g. "OK\0T..." from a previous "WAIT").
String sessionReplyFromBytes(List<int> bytes) {
  final out = StringBuffer();
  for (final b in bytes) {
    if (b == 0) break;
    if (b >= 32 && b <= 126) out.writeCharCode(b);
  }
  return out.toString().trim();
}

bool isSessionStatus(String text) =>
    text == 'OK' ||
    text == 'FAIL' ||
    text == 'ERR' ||
    text == 'WAIT' ||
    text == 'PONG';

/// The live `lmyc://` link on the terminal payload characteristic.
PairingLink? linkFromPayloadBytes(List<int> bytes) {
  final uri = Uri.tryParse(sessionReplyFromBytes(bytes));
  if (uri == null) return null;
  return PairingLink.parse(uri);
}

/// Pull `oob=` from the terminal payload characteristic (lmyc:// URI bytes).
String? oobFromPayloadBytes(List<int> bytes) =>
    linkFromPayloadBytes(bytes)?.oob;

/// True for an advertised name the terminal uses.
bool isLmycBleName(String name) =>
    name.toUpperCase().startsWith(kLmycBlePrefix);

/// What a Discover row shows for one advertised name.
class TerminalName {
  const TerminalName({required this.boat, required this.tag});

  /// The boat ID, or null for firmware that advertises only `LMYC-XXXX`.
  final String? boat;

  /// Last two Bluetooth MAC bytes, as on the terminal's BLE name row.
  final String tag;

  static final _withBoat = RegExp(r'^(.+)-([0-9A-F]{4})$');
  static final _tagOnly = RegExp(r'^[0-9A-F]{4}$');

  /// Splits `LMYC-<boat>-<XXXX>` (or the older `LMYC-<XXXX>`).
  static TerminalName parse(String bleName) {
    final rest = bleName.toUpperCase().startsWith(kLmycBlePrefix)
        ? bleName.substring(kLmycBlePrefix.length)
        : bleName;
    final full = _withBoat.firstMatch(rest);
    if (full != null) {
      return TerminalName(boat: full.group(1), tag: full.group(2)!);
    }
    if (_tagOnly.hasMatch(rest)) {
      return TerminalName(boat: null, tag: rest);
    }
    return TerminalName(boat: rest.isEmpty ? null : rest, tag: '');
  }

  String get title => boat ?? 'Terminal $tag';
}

/// Plain words for a Discover row. RSSI is in dBm.
String signalWord(int rssi) {
  if (rssi >= -65) return 'Strong signal';
  if (rssi >= -80) return 'Good signal';
  return 'Weak signal';
}
