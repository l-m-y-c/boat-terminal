import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lmyc_app/boat_wifi.dart';
import 'package:lmyc_app/diagnostics.dart';
import 'package:lmyc_app/event_log.dart';
import 'package:lmyc_app/terminal_stats.dart';

import 'terminal_stats_test.dart' show firmwareJson;

final _t = DateTime(2026, 10, 22, 16, 40);

const _good = BoatWifiStatus(
  onBoat: true,
  relayRunning: true,
  relayListening: true,
  relayUpstream: true,
  relayDataAgeMs: 400,
  relayClients: 2,
);

DiagnosticsSnapshot snap({
  BleState ble = BleState.paired,
  String detail = '',
  String? ssid = 'LMYC-BENCH-01',
  BoatWifiStatus? wifi = _good,
  String state = 'live',
  String? problem,
}) => DiagnosticsSnapshot(
  takenAt: _t,
  boat: 'BENCH-01',
  bleName: 'LMYC-BENCH-01',
  bleState: ble,
  bleLabel: ble == BleState.paired ? 'Connected' : 'Failed',
  bleDetail: detail,
  rssi: -62,
  sessionReply: 'OK',
  ssid: ssid,
  ip: ssid == null ? null : '192.168.4.1',
  wifiProblem: problem,
  wifi: wifi,
  stats: TerminalStats.parse(firmwareJson(state: state)),
  statsAt: _t.subtract(const Duration(seconds: 1)),
);

void main() {
  group('verdict walks the chain', () {
    test('nothing yet', () {
      final v = diagnosticsVerdict(DiagnosticsSnapshot(takenAt: _t));
      expect(v.mark, LinkMark.idle);
      expect(v.title, 'Not connected to a boat');
    });
    test('Bluetooth failed before the Wi-Fi', () {
      final v = diagnosticsVerdict(
        snap(
          ble: BleState.failed,
          detail: 'Pairing failed: x',
          ssid: null,
          wifi: null,
        ),
      );
      expect(v.mark, LinkMark.fail);
      expect(v.detail, 'Pairing failed: x');
    });
    test('phone off the boat Wi-Fi', () {
      final v = diagnosticsVerdict(
        snap(wifi: const BoatWifiStatus(onBoat: false, relayRunning: true)),
      );
      expect(v.title, 'Phone is not on LMYC-BENCH-01');
    });
    test('on the Wi-Fi but no data', () {
      final v = diagnosticsVerdict(
        snap(
          wifi: const BoatWifiStatus(
            onBoat: true,
            relayRunning: true,
            relayListening: true,
            relayUpstream: true,
            relayDataAgeMs: 40000,
          ),
        ),
      );
      expect(v.title, 'No data from the terminal');
    });
    test('port taken', () {
      final v = diagnosticsVerdict(
        snap(
          wifi: const BoatWifiStatus(
            onBoat: true,
            relayRunning: true,
            relayUpstream: true,
            relayDataAgeMs: 100,
          ),
        ),
      );
      expect(v.title, 'Chart apps cannot connect');
    });
    test('bus fault', () {
      expect(
        diagnosticsVerdict(snap(state: 'bus_off')).title,
        'NMEA 2000 bus fault',
      );
    });
    test('Bluetooth dropped but chart apps fine', () {
      final v = diagnosticsVerdict(snap(ble: BleState.failed));
      expect(v.mark, LinkMark.done);
      expect(v.title, 'Chart apps are getting data');
    });
    test('all good', () {
      expect(diagnosticsVerdict(snap()).title, 'All links good');
    });
    test('Wi-Fi problem', () {
      expect(
        diagnosticsVerdict(snap(problem: 'No Wi-Fi details')).mark,
        LinkMark.fail,
      );
    });
  });

  test('cards show the relay fields the old screen hid', () {
    final relay = diagnosticsCards(
      snap(),
      null,
    ).firstWhere((c) => c.key == 'relay');
    expect(relay.mark, LinkMark.done);
    final rows = Map.fromEntries(relay.rows.map((r) => MapEntry(r.$1, r.$2)));
    expect(rows['Link to terminal'], 'Connected');
    expect(rows['Last data'], 'Just now');
    expect(rows['Service'], 'Running');
  });

  test('every card has help, and so do the verdict and events', () {
    for (final c in diagnosticsCards(snap(), null)) {
      expect(diagnosticsHelp.containsKey(c.key), isTrue, reason: c.key);
    }
    expect(diagnosticsHelp.containsKey('verdict'), isTrue);
    expect(diagnosticsHelp.containsKey('events'), isTrue);
  });

  test('status.json as hub_http.cpp prints it', () {
    final hub = HubStatus.parse(
      '{"boat":"BENCH-01","sailboat_id":504,"tcp":1,"nmea_good":9876,'
      '"nmea_bad":3,"inject_local":false,"snapshot":false,'
      '"data":${firmwareJson()}}\n',
    )!;
    expect(hub.tcpClients, 1);
    expect(hub.nmeaGood, 9876);
    expect(hub.nmeaBad, 3);
    expect(HubStatus.parse('nope'), isNull);
    final bus = diagnosticsCards(snap(), hub).firstWhere((c) => c.key == 'bus');
    expect(bus.rows.map((r) => r.$1), contains('0183 lines rejected'));
  });

  test('event log keeps the newest 200', () {
    final log = EventLog(clock: () => _t);
    for (var i = 0; i < 250; i++) {
      log.add('e$i');
    }
    expect(log.events.length, 200);
    expect(log.events.first.text, 'e50');
    expect(log.events.last.text, 'e249');
    log.add('   ');
    expect(log.events.length, 200);
  });

  test('report has every section and the events, oldest first', () {
    final log = EventLog(clock: () => _t)
      ..add('Bluetooth: Scanning')
      ..add('Bluetooth: Paired');
    final r = diagnosticsReport(snap(), events: log.events);
    expect(r, contains('[OK] All links good'));
    for (final t in [
      'Bluetooth',
      'Boat Wi-Fi',
      'Phone Wi-Fi',
      'Relay on this phone',
      'Chart apps',
      'Boat\'s data',
    ]) {
      expect(r, contains('] $t:'));
    }
    expect(r.indexOf('Scanning'), lessThan(r.indexOf('Paired')));
    expect(r.toLowerCase(), isNot(contains('psk')));
  });

  group('screen', () {
    Future<void> pump(WidgetTester tester, Widget child) async {
      await tester.binding.setSurfaceSize(const Size(420, 2400));
      addTearDown(() => tester.binding.setSurfaceSize(null));
      await tester.pumpWidget(MaterialApp(home: Scaffold(body: child)));
    }

    testWidgets('verdict, six cards, and help on tap', (tester) async {
      await pump(tester, DiagnosticsView(snapshot: snap(), log: EventLog()));
      expect(find.text('All links good'), findsOneWidget);
      for (final k in [
        'bluetooth',
        'boat_wifi',
        'phone_wifi',
        'relay',
        'chart_apps',
        'bus',
      ]) {
        expect(find.byKey(ValueKey('diag-$k')), findsOneWidget);
      }
      await tester.tap(find.byKey(const ValueKey('diag-relay')));
      await tester.pumpAndSettle();
      expect(
        find.textContaining('127.0.0.1 port 10110, which is'),
        findsOneWidget,
      );
    });

    testWidgets('copy puts the report on the clipboard', (tester) async {
      String? copied;
      tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        SystemChannels.platform,
        (call) async {
          if (call.method == 'Clipboard.setData') {
            copied = (call.arguments as Map)['text'] as String;
          }
          return null;
        },
      );
      final log = EventLog()..add('hello event');
      await pump(tester, DiagnosticsView(snapshot: snap(), log: log));
      expect(find.text('hello event'), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('diag-copy')));
      await tester.pumpAndSettle();
      expect(copied, contains('All links good'));
      expect(copied, contains('hello event'));
      expect(find.text('Report copied'), findsOneWidget);
    });

    testWidgets('forget asks first', (tester) async {
      var forgot = 0;
      await pump(
        tester,
        DiagnosticsView(
          snapshot: snap(),
          log: EventLog(),
          onForget: () async => forgot++,
          forgetLabel: 'BENCH-01',
        ),
      );
      await tester.tap(find.text('Forget BENCH-01'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Cancel'));
      await tester.pumpAndSettle();
      expect(forgot, 0);
      await tester.tap(find.text('Forget BENCH-01'));
      await tester.pumpAndSettle();
      await tester.tap(find.widgetWithText(FilledButton, 'Forget'));
      await tester.pumpAndSettle();
      expect(forgot, 1);
    });

    testWidgets('reconnect only when offered', (tester) async {
      await pump(tester, DiagnosticsView(snapshot: snap(), log: EventLog()));
      expect(find.byKey(const ValueKey('diag-reconnect')), findsNothing);
      var n = 0;
      await pump(
        tester,
        DiagnosticsView(
          snapshot: snap(ble: BleState.failed),
          log: EventLog(),
          onReconnect: () => n++,
        ),
      );
      await tester.tap(find.byKey(const ValueKey('diag-reconnect')));
      expect(n, 1);
    });
  });
}
