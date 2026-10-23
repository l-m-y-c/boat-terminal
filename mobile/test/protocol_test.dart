import 'dart:convert';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:lmyc_app/maintenance_api.dart';
import 'package:lmyc_app/maintenance_book.dart';
import 'package:lmyc_app/nmea_sentences.dart';
import 'package:lmyc_app/pairing_link.dart';
import 'package:lmyc_app/protocol.dart';
import 'package:lmyc_app/utf8_formatter.dart';
import 'package:lmyc_app/utf8_limit.dart';

void main() {
  group('pairing link', () {
    const good =
        'lmyc://pair?v=1&boat=BENCH-01&tid=WS7-001&ble=LMYC-D649'
        '&oob=0123456789abcdef0123456789abcdef';

    test('the terminal QR parses', () {
      final link = PairingLink.parse(Uri.parse(good))!;
      expect(link.version, '1');
      expect(link.boatId, 'BENCH-01');
      expect(link.terminalId, 'WS7-001');
      expect(link.bleName, 'LMYC-D649');
      expect(link.oob, '0123456789abcdef0123456789abcdef');
      expect(link.boatName, isNull);
    });

    test('a newer terminal adds its display name', () {
      final link = PairingLink.parse(Uri.parse('$good&name=Baca%20Lele'))!;
      expect(link.boatId, 'BENCH-01');
      expect(link.boatName, 'Baca Lele');
    });

    test('another host or version is refused with a reason', () {
      final other = Uri.parse('lmyc://book?v=1');
      expect(PairingLink.parse(other), isNull);
      expect(PairingLink.rejection(other), contains('lmyc://pair'));

      final v2 = Uri.parse(good.replaceFirst('v=1', 'v=2'));
      expect(PairingLink.parse(v2), isNull);
      expect(PairingLink.rejection(v2), contains('v2'));

      expect(PairingLink.parse(Uri.parse('https://lmyc.ca/pair?v=1')), isNull);
    });

    test('a malformed oob is dropped, not written', () {
      final shortOob = good.replaceFirst(RegExp(r'oob=.*'), 'oob=0123');
      expect(PairingLink.parse(Uri.parse(shortOob))!.oob, isNull);
      final notHex = good.replaceFirst(RegExp(r'oob=.*'), 'oob=${'zz' * 16}');
      expect(PairingLink.parse(Uri.parse(notHex))!.oob, isNull);
    });

    test('oob hex becomes 16 raw bytes', () {
      final bytes = oobBytesFromHex('00ff' * 8)!;
      expect(bytes.length, 16);
      expect(bytes.take(2), [0x00, 0xff]);
      expect(oobBytesFromHex('00ff'), isNull);
    });

    test('a session reply stops at NUL and drops junk', () {
      expect(sessionReplyFromBytes([0x4f, 0x4b, 0x00, 0x54]), 'OK');
      expect(sessionReplyFromBytes(utf8.encode(' WAIT\n')), 'WAIT');
      expect(isSessionStatus('OK'), isTrue);
      expect(isSessionStatus('OKAY'), isFalse);
    });

    test('the payload characteristic yields the oob', () {
      expect(
        oobFromPayloadBytes(utf8.encode('$good\u0000junk')),
        '0123456789abcdef0123456789abcdef',
      );
      expect(oobFromPayloadBytes(utf8.encode('hello')), isNull);
    });

    test('terminal names', () {
      expect(isLmycBleName('LMYC-D649'), isTrue);
      expect(isLmycBleName('lmyc-d649'), isTrue);
      expect(isLmycBleName('Pixel Buds'), isFalse);
    });

    test('the payload link also carries the boat and terminal', () {
      final link = linkFromPayloadBytes(utf8.encode('$good\u0000junk'));
      expect(link?.boatId, isNotNull);
      expect(link?.terminalId, isNotNull);
    });

    test('a Discover row shows the boat name from the BLE name', () {
      final named = TerminalName.parse('LMYC-BENCH-01-3F2A');
      expect(named.boat, 'BENCH-01');
      expect(named.tag, '3F2A');
      expect(named.title, 'BENCH-01');

      final old = TerminalName.parse('LMYC-D649');
      expect(old.boat, isNull);
      expect(old.title, 'Terminal D649');
    });

    test('signal strength reads as words', () {
      expect(signalWord(-50), 'Strong signal');
      expect(signalWord(-72), 'Good signal');
      expect(signalWord(-90), 'Weak signal');
    });
  });

  group('NMEA sentences', () {
    test('checksum matches a known sentence', () {
      const rmc =
          r'$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A';
      expect(nmeaChecksumOk(rmc), isTrue);
      expect(nmeaChecksumOk(rmc.replaceFirst('*6A', '*6B')), isFalse);
      expect(
        nmeaChecksumOk(
          '!AIVDM,2,1,0,A,55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6C,0*6B',
        ),
        isTrue,
      );
      expect(nmeaChecksumOk('GPRMC no dollar*00'), isFalse);
    });

    test('lat and lon use degrees and minutes with hemisphere', () {
      expect(nmeaLat(49.2827), '4916.9620,N');
      expect(nmeaLon(-123.1207), '12307.2420,W');
      expect(nmeaLat(-0.5), '0030.0000,S');
    });

    test('59.99996 minutes carries into the degree', () {
      expect(nmeaLat(48.9999999), '4900.0000,N');
    });

    test('RMC and GGA from a fix are valid sentences', () {
      final utc = DateTime.utc(2026, 10, 19, 7, 5, 9);
      final rmc = buildRmc(
        utc: utc,
        lat: 49.2827,
        lon: -123.1207,
        sogKn: 5.04,
        cogDeg: double.nan,
      );
      expect(
        rmc,
        startsWith(
          r'$GPRMC,070509.00,A,4916.9620,N,12307.2420,W,5.0,0.0,191026,,,A*',
        ),
      );
      expect(nmeaChecksumOk(rmc), isTrue);
      final gga = buildGga(
        utc: utc,
        lat: 49.2827,
        lon: -123.1207,
        altitudeM: 3,
      );
      expect(
        gga,
        startsWith(r'$GPGGA,070509.00,4916.9620,N,12307.2420,W,1,08,1.0,3.0,M'),
      );
      expect(nmeaChecksumOk(gga), isTrue);
    });

    test('an unknown speed is sent as zero, not negative', () {
      final rmc = buildRmc(
        utc: DateTime.utc(2026),
        lat: 0,
        lon: 0,
        sogKn: -1.9,
        cogDeg: -1,
      );
      expect(rmc, contains(',0.0,0.0,'));
    });
  });

  group('UTF-8 limits', () {
    test('clip keeps whole characters within the byte budget', () {
      expect(clipUtf8('hello', 10), 'hello');
      expect(clipUtf8('café', 4), 'caf');
      expect(utf8.encode(clipUtf8('€€€', 7)).length, 6);
      expect(clipUtf8('a🚤b', 4), 'a');
      expect(clipUtf8('a🚤b', 5), 'a🚤');
    });

    test('the formatter stops typing at the byte limit', () {
      final f = Utf8LengthLimitingFormatter(4);
      const old = TextEditingValue(text: 'ca');
      final next = f.formatEditUpdate(
        old,
        const TextEditingValue(
          text: 'cafés',
          selection: TextSelection.collapsed(offset: 5),
        ),
      );
      expect(next.text, 'caf');
      expect(utf8.encode(next.text).length, lessThanOrEqualTo(4));
    });
  });

  test('the book page offers exactly the categories and priorities the boat accepts', () {
    expect(kBookCategoryOrder.toSet(), kMaintCategories.toSet());
    expect(kBookPriorityOrder.toSet(), kMaintPriorities.toSet());
  });

  test('club calls go through the route hook, boat calls do not', () async {
    final routed = <String>[];
    final inRoute = <bool>[];
    var routing = false;
    Future<T> route<T>(Future<T> Function() call) async {
      routing = true;
      try {
        return await call();
      } finally {
        routing = false;
      }
    }

    final client = MockClient((request) async {
      inRoute.add(routing);
      routed.add('${routing ? 'club' : 'boat'} ${request.url.path}');
      if (request.url.host == 'club.example') {
        return http.Response('{"wp_id": 9}', 201);
      }
      return http.Response('{"sync":"synced"}', 200);
    });
    final club = ClubMaintenanceClient(
      rawBaseUrl: 'http://club.example',
      client: client,
      route: route,
    );
    final book = TerminalBook(host: kSoftApIp, client: client);
    final result = await club.uploadPending([
      {
        'id': 'aabbccddeeff0011',
        'sync': 'pending',
        'title': 'Bilge pump',
        'category': 'engine',
        'priority': 'high',
        'sailboat_id': 504,
      },
    ], book);
    expect(result.uploaded, 1);
    expect(routed, [
      'club /wp-json/lmyc/v1/boats/504/maintenance',
      'boat /maintenance/ack',
    ]);
  });
}
