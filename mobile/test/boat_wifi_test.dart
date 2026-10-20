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

  test('link rows stay idle until each step succeeds', () {
    final waiting = linkRows(
      blePaired: true,
      joining: false,
      joined: false,
      reached: null,
    );
    expect(waiting.map((r) => r.value).toList(), [
      'Paired',
      'Name not received',
      'Waiting for the boat name',
      'Not checked yet',
    ]);
    expect(waiting[0].mark, LinkMark.done);
    expect(waiting[2].mark, LinkMark.idle);
  });

  test('a joined app that cannot open port 10110 is a failed reach', () {
    const info = BoatWifiInfo(
      ssid: 'LMYC-BENCH-01',
      psk: 'lmycboat',
      ip: '192.168.4.1',
    );
    final rows = linkRows(
      blePaired: true,
      info: info,
      joining: false,
      joined: true,
      reached: false,
    );
    expect(rows[1].value, 'LMYC-BENCH-01');
    expect(rows[2].mark, LinkMark.done);
    expect(rows[3].mark, LinkMark.fail);
    expect(rows[3].value, contains('No route'));
  });

  test('an open NMEA port is the last green row', () {
    const info = BoatWifiInfo(
      ssid: 'LMYC-BENCH-01',
      psk: 'lmycboat',
      ip: '192.168.4.1',
    );
    final rows = linkRows(
      blePaired: true,
      info: info,
      joining: true,
      joined: true,
      reached: true,
    );
    expect(rows[3].value, '192.168.4.1 port 10110 is open');
    expect(rows[3].mark, LinkMark.done);
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
