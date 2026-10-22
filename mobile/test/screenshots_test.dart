// Renders the on-board screens to PNGs for review, with real fonts.
//   flutter test test/screenshots_test.dart --update-goldens \
//     --dart-define=SCREENSHOTS=true
// Files land in test/screenshots/ (not committed). Skipped otherwise.
import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lmyc_app/boat_wifi.dart';
import 'package:lmyc_app/counter_help.dart';
import 'package:lmyc_app/diagnostics.dart';
import 'package:lmyc_app/event_log.dart';
import 'package:lmyc_app/instruments.dart';
import 'package:lmyc_app/journey.dart';
import 'package:lmyc_app/nmea_data_page.dart';
import 'package:lmyc_app/nmea_sentences.dart';
import 'package:lmyc_app/terminal_stats.dart';
import 'package:lmyc_app/ui_kit.dart';

const _enabled = bool.fromEnvironment('SCREENSHOTS');

Future<void> _loadFonts() async {
  final root = Platform.environment['FLUTTER_ROOT']!;
  final dir = '$root/bin/cache/artifacts/material_fonts';
  Future<void> load(String family, List<String> files) async {
    final loader = FontLoader(family);
    for (final f in files) {
      final bytes = File('$dir/$f').readAsBytesSync();
      loader.addFont(Future.value(ByteData.sublistView(bytes)));
    }
    await loader.load();
  }

  await load('Roboto', [
    'Roboto-Regular.ttf',
    'Roboto-Medium.ttf',
    'Roboto-Bold.ttf',
  ]);
  await load('MaterialIcons', ['MaterialIcons-Regular.otf']);
}

const _json =
    '{"v":1,"src":"sim","state":"sim","fps":312,"fps_peak":540,"mps":120,'
    '"frames":1234567,"msgs":45678,"q_cap":2048,"q_now":0,"q_peak":37,'
    '"lost":0,"lost_q":0,"lost_drv":0,"lost_hw":0,"bus_err":0,"rec":0,'
    '"pgns":14,"devices":6,"age_ms":12,"up_s":11520,'
    '"apps":1,"out":5678,"out_lost":2,"udp_lost":1,"out_peak":1229,"out_cap":8192}';

/// The on-board shell as main.dart lays it out: title with badge, compact
/// strip, the tab's content, and the bottom bar.
Widget _shell(int tab, Widget body) => MaterialApp(
  debugShowCheckedModeBanner: false,
  theme: ThemeData(
    fontFamily: 'Roboto',
    colorScheme: ColorScheme.fromSeed(seedColor: kNavy),
    appBarTheme: const AppBarTheme(
      centerTitle: true,
      backgroundColor: kNavy,
      foregroundColor: Colors.white,
    ),
  ),
  home: Scaffold(
    backgroundColor: const Color(0xFFF7F9FB),
    appBar: AppBar(
      title: const BoatTitle(name: 'BENCH-01', simulated: true),
      actions: [
        IconButton(onPressed: () {}, icon: const Icon(Icons.settings_outlined)),
      ],
    ),
    body: Column(
      children: [
        const Padding(
          padding: EdgeInsets.fromLTRB(28, 10, 28, 2),
          child: JourneyBar(current: JourneyStop.arrived, compact: true),
        ),
        Expanded(child: body),
      ],
    ),
    bottomNavigationBar: NavigationBar(
      selectedIndex: tab,
      destinations: const [
        NavigationDestination(icon: Icon(Icons.sailing), label: 'Boat'),
        NavigationDestination(icon: Icon(Icons.explore), label: 'Instruments'),
        NavigationDestination(icon: Icon(Icons.analytics), label: 'NMEA data'),
        NavigationDestination(
          icon: Icon(Icons.troubleshoot),
          label: 'Diagnostics',
        ),
      ],
    ),
  ),
);

void main() {
  setUpAll(() async {
    if (_enabled) await _loadFonts();
  });

  Future<void> phone(WidgetTester tester) async {
    tester.view.physicalSize = const Size(1080, 2340);
    tester.view.devicePixelRatio = 2.625;
    addTearDown(tester.view.reset);
  }

  testWidgets('instruments', skip: !_enabled, (tester) async {
    await phone(tester);
    final lines = StreamController<String>();
    addTearDown(lines.close);
    await tester.pumpWidget(
      _shell(1, InstrumentsView(lines: () => lines.stream)),
    );
    lines
      ..add(nmeaSentence('SDDPT,8.4,0.0'))
      ..add(
        nmeaSentence(
          'GPRMC,120000.00,A,4916.0,N,12311.0,W,5.6,231.0,221026,,,A',
        ),
      )
      ..add(nmeaSentence('WIMWV,320.0,R,14.0,N,A'));
    await tester.pump();
    await expectLater(
      find.byType(MaterialApp),
      matchesGoldenFile('screenshots/instruments.png'),
    );
  });

  testWidgets('instruments empty', skip: !_enabled, (tester) async {
    await phone(tester);
    final lines = StreamController<String>();
    addTearDown(lines.close);
    await tester.pumpWidget(
      _shell(1, InstrumentsView(lines: () => lines.stream)),
    );
    await expectLater(
      find.byType(MaterialApp),
      matchesGoldenFile('screenshots/instruments_empty.png'),
    );
  });

  testWidgets('nmea data', skip: !_enabled, (tester) async {
    await phone(tester);
    await tester.pumpWidget(
      _shell(
        2,
        NmeaDataView(
          stats: TerminalStats.parse(_json),
          updatedAt: null,
          supported: true,
        ),
      ),
    );
    await expectLater(
      find.byType(MaterialApp),
      matchesGoldenFile('screenshots/nmea_data.png'),
    );
    await tester.ensureVisible(
      find.byKey(const ValueKey('metric-not_delivered')),
    );
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('metric-not_delivered')));
    await tester.pumpAndSettle();
    expect(find.text(counterHelp['not_delivered']!.title), findsWidgets);
    await expectLater(
      find.byType(MaterialApp),
      matchesGoldenFile('screenshots/help_not_delivered.png'),
    );
  });

  testWidgets('simulation sheet', skip: !_enabled, (tester) async {
    await phone(tester);
    await tester.pumpWidget(_shell(0, const SizedBox()));
    await tester.tap(find.byKey(const ValueKey('simulation-badge')));
    await tester.pumpAndSettle();
    await expectLater(
      find.byType(MaterialApp),
      matchesGoldenFile('screenshots/simulation_sheet.png'),
    );
  });

  DiagnosticsSnapshot diag({BoatWifiStatus? wifi}) => DiagnosticsSnapshot(
    takenAt: DateTime(2026, 10, 22, 16, 40, 5),
    boat: 'BENCH-01',
    terminalId: 'T-0001',
    bleName: 'LMYC-BENCH-01',
    bleState: BleState.paired,
    bleLabel: 'Connected',
    bleDetail: 'Paired with BENCH-01\nTerminal confirmed OOB.',
    rssi: -61,
    sessionReply: 'OK',
    ssid: 'LMYC-BENCH-01',
    ip: '192.168.4.1',
    wifi:
        wifi ??
        const BoatWifiStatus(
          onBoat: true,
          relayRunning: true,
          relayListening: true,
          relayUpstream: true,
          relayDataAgeMs: 420,
          relayClients: 1,
        ),
    stats: TerminalStats.parse(_json),
    statsAt: DateTime(2026, 10, 22, 16, 40, 4),
  );

  EventLog sampleLog() {
    var t = DateTime(2026, 10, 22, 16, 39, 30);
    final log = EventLog(clock: () => t = t.add(const Duration(seconds: 2)));
    for (final e in [
      'Bluetooth: Scanning',
      'Found LMYC-BENCH-01 (-61 dBm) — connecting…',
      'Bluetooth: Confirming',
      'Bluetooth: Paired',
      'boat wifi: BoatWifiStatus(onBoat: true, relay: listening, upstream: true, receiving: true, clients: 1)',
    ]) {
      log.add(e);
    }
    return log;
  }

  testWidgets('diagnostics', skip: !_enabled, (tester) async {
    await phone(tester);
    await tester.pumpWidget(
      _shell(3, DiagnosticsView(snapshot: diag(), log: sampleLog())),
    );
    await expectLater(
      find.byType(MaterialApp),
      matchesGoldenFile('screenshots/diagnostics.png'),
    );
    await tester.drag(
      find.byKey(const ValueKey('diagnostics')),
      const Offset(0, -1500),
    );
    await tester.pumpAndSettle();
    await expectLater(
      find.byType(MaterialApp),
      matchesGoldenFile('screenshots/diagnostics_lower.png'),
    );
  });

  testWidgets('diagnostics failing', skip: !_enabled, (tester) async {
    await phone(tester);
    await tester.pumpWidget(
      _shell(
        3,
        DiagnosticsView(
          snapshot: diag(
            wifi: const BoatWifiStatus(onBoat: false, relayRunning: true),
          ),
          log: sampleLog(),
          onReconnect: () {},
        ),
      ),
    );
    await expectLater(
      find.byType(MaterialApp),
      matchesGoldenFile('screenshots/diagnostics_failing.png'),
    );
  });
}
