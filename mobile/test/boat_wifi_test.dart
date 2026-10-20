import 'package:flutter_test/flutter_test.dart';
import 'package:lmyc_app/boat_wifi.dart';

void main() {
  test('wifi bootstrap JSON becomes the SoftAP details', () {
    final info = parseWifiBootstrap(
      '{"ssid":"LMYC-BENCH-01","psk":"lmycboat","ip":"192.168.4.1"}',
    );
    expect(info, isNotNull);
    expect(info!.ssid, 'LMYC-BENCH-01');
    expect(info.psk, 'lmycboat');
    expect(info.ip, '192.168.4.1');
  });

  test('a passphrase with quotes still parses', () {
    final info = parseWifiBootstrap(
      '{"ssid":"LMYC-BENCH-01","psk":"ab\\"cd\\\\ef","ip":"192.168.4.1"}',
    );
    expect(info?.psk, 'ab"cd\\ef');
  });

  test('WAIT and a short passphrase are not a network', () {
    expect(parseWifiBootstrap('WAIT'), isNull);
    expect(parseWifiBootstrap('ERR'), isNull);
    expect(
      parseWifiBootstrap(
        '{"ssid":"LMYC-BENCH-01","psk":"short","ip":"192.168.4.1"}',
      ),
      isNull,
    );
    expect(
      parseWifiBootstrap(
        '{"ssid":"LMYC-BENCH-01","psk":"lmycboat","ip":"192.168.4.1"}\u0000WAIT',
      ),
      isNotNull,
    );
  });
}
