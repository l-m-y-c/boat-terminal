import 'package:flutter_test/flutter_test.dart';
import 'package:lmyc_app/boat_setup.dart';
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

  test('only an LMYC hotspot is saved on the phone', () {
    expect(
      parseWifiBootstrap(
        '{"ssid":"CoffeeShop","psk":"lmycboat","ip":"192.168.4.1"}',
      ),
      isNull,
    );
  });

  test('a passphrase with quotes still parses', () {
    final info = parseWifiBootstrap(
      '{"ssid":"LMYC-BENCH-01","psk":"ab\\"cd\\\\ef","ip":"192.168.4.1"}',
    );
    expect(info?.psk, 'ab"cd\\ef');
  });

  const info = BoatWifiInfo(
    ssid: 'LMYC-BENCH-01',
    psk: 'lmycboat',
    ip: '192.168.4.1',
  );
  const away = BoatWifiStatus(onBoat: false);
  // Mobile data is the default network: the relay still serves chart apps.
  const cellular = BoatWifiStatus(
    onBoat: true,
    relayRunning: true,
    relayListening: true,
    relayUpstream: true,
    relayDataAgeMs: 400,
    relayClients: 1,
  );
  const silent = BoatWifiStatus(
    onBoat: true,
    isDefault: true,
    relayRunning: true,
    relayListening: true,
  );
  const stale = BoatWifiStatus(
    onBoat: true,
    relayRunning: true,
    relayListening: true,
    relayUpstream: true,
    relayDataAgeMs: kRelayStaleMs + 1,
  );
  const portTaken = BoatWifiStatus(
    onBoat: true,
    relayRunning: true,
    relayUpstream: true,
    relayDataAgeMs: 400,
  );

  test('link rows stay idle until the phone has been checked', () {
    final rows = linkRows(blePaired: true);
    expect(rows.map((r) => r.value).toList(), [
      'Paired',
      'Name not received',
      'Not checked yet',
      'Not checked yet',
    ]);
    expect(rows[0].mark, LinkMark.done);
    expect(rows[3].mark, LinkMark.idle);
  });

  test('the relay serves chart apps with mobile data as the default', () {
    expect(cellular.ready, isTrue);
    final rows = linkRows(blePaired: true, info: info, status: cellular);
    expect(rows[2].mark, LinkMark.done);
    expect(rows[3].mark, LinkMark.done);
    expect(rows[3].value, '127.0.0.1 port 10110, 1 connected');
  });

  test('no data from the terminal is a failed chart row', () {
    final rows = linkRows(blePaired: true, info: info, status: silent);
    expect(rows[3].mark, LinkMark.fail);
    expect(rows[3].value, contains('192.168.4.1'));
  });

  test('data older than five seconds is not receiving', () {
    expect(stale.receiving, isFalse);
    expect(stale.ready, isFalse);
  });

  test('another app on the loopback port is named', () {
    final rows = linkRows(blePaired: true, info: info, status: portTaken);
    expect(portTaken.ready, isFalse);
    expect(rows[3].value, contains('Another app'));
  });

  group('setup step', () {
    WifiSetup step({
      bool reading = false,
      BoatWifiInfo? wifi = info,
      BoatWifiStatus? status,
      String? problem,
    }) => wifiSetupFor(
      reading: reading,
      info: wifi,
      status: status,
      problem: problem,
    );

    test('reading the Bluetooth details comes first', () {
      expect(step(reading: true, wifi: null), WifiSetup.reading);
    });

    test('no Wi-Fi details is a problem the user can retry', () {
      expect(step(wifi: null), WifiSetup.problem);
      expect(step(problem: 'lost'), WifiSetup.problem);
    });

    test('the relay joins, then the stop completes on data', () {
      expect(step(status: away), WifiSetup.joining);
      expect(step(), WifiSetup.joining);
      expect(step(status: cellular), WifiSetup.ready);
    });

    test('a refused Connect dialog is a problem the user can retry', () {
      const refused = BoatWifiStatus(onBoat: false, relayDeclined: true);
      expect(step(status: refused), WifiSetup.problem);
      expect(setupCopy(WifiSetup.problem, 'x').action, SetupAction.retry);
    });

    test('on the boat without data waits for the terminal', () {
      expect(step(status: silent), WifiSetup.noAnswer);
      expect(step(status: stale), WifiSetup.noAnswer);
    });

    test('only a problem asks the user to press anything', () {
      for (final s in WifiSetup.values) {
        final copy = setupCopy(s, 'LMYC-BENCH-01');
        expect(copy.action == null, copy.actionLabel == null, reason: '$s');
        expect(copy.action != null, s == WifiSetup.problem, reason: '$s');
      }
    });

    test('the ready step tells the member what to type', () {
      final copy = setupCopy(WifiSetup.ready, 'LMYC-BENCH-01');
      expect(copy.body, contains('127.0.0.1'));
      expect(copy.body, contains('10110'));
    });
  });

  test('the app reaches the terminal web server through the relay', () {
    final uri = Uri.http(kTerminalHttpHost, '/maintenance');
    expect(uri.host, '127.0.0.1');
    expect(uri.port, kRelayHttpPort);
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

  test('the terminal counts as gone only after two missed heartbeats', () {
    expect(kRelayStaleMs, greaterThanOrEqualTo(2 * kHeartbeatMs));
    BoatWifiStatus at(int age) => BoatWifiStatus(
      onBoat: true,
      relayRunning: true,
      relayListening: true,
      relayUpstream: true,
      relayDataAgeMs: age,
    );
    expect(at(kHeartbeatMs + 5000).receiving, isTrue);
    expect(at(kRelayStaleMs - 1).receiving, isTrue);
    expect(at(kRelayStaleMs).receiving, isFalse);
  });

  test('a refused Connect dialog shows a problem with Try again', () {
    const info = BoatWifiInfo(ssid: 'LMYC', psk: 'password', ip: '192.168.4.1');
    const refused = BoatWifiStatus(
      onBoat: false,
      relayRunning: true,
      relayDeclined: true,
    );
    expect(
      wifiSetupFor(reading: false, info: info, status: refused),
      WifiSetup.problem,
    );
    expect(setupCopy(WifiSetup.problem, 'LMYC').action, SetupAction.retry);
  });
}
