import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lmyc_app/nmea_data_page.dart';
import 'package:lmyc_app/terminal_stats.dart';

/// Laid out exactly as firmware/src/terminal_stats.cpp prints it.
String firmwareJson({
  String src = 'n2k',
  String state = 'live',
  int lost = 0,
  int lostQ = 0,
  String age = '12',
}) =>
    '{"v":1,"src":"$src","state":"$state","fps":312,"fps_peak":540,"mps":120,'
    '"frames":1234567,"msgs":45678,"q_cap":2048,"q_now":0,"q_peak":37,'
    '"lost":$lost,"lost_q":$lostQ,"lost_drv":0,"lost_hw":0,"bus_err":0,"rec":0,'
    '"pgns":14,"devices":6,"age_ms":$age,"up_s":11520,'
    '"apps":1,"out":5678,"out_lost":2,"udp_lost":1,"out_peak":1229,"out_cap":8192}';

Future<void> pumpPage(WidgetTester tester, TerminalStats stats) async {
  await tester.binding.setSurfaceSize(const Size(400, 2000));
  addTearDown(() => tester.binding.setSurfaceSize(null));
  await tester.pumpWidget(
    MaterialApp(
      home: Scaffold(
        body: NmeaDataView(stats: stats, updatedAt: null, supported: true),
      ),
    ),
  );
}

void main() {
  group('terminal stats', () {
    test('the firmware JSON parses field by field', () {
      final s = TerminalStats.parse('${firmwareJson()}\u0000junk')!;
      expect(s.mode, DataMode.nmea2000);
      expect(s.state, BusState.live);
      expect(s.framesPerSecond, 312);
      expect(s.framesPerSecondPeak, 540);
      expect(s.messagesPerSecond, 120);
      expect(s.frames, 1234567);
      expect(s.bufferCapacity, 2048);
      expect(s.bufferPeak, 37);
      expect(s.pgnTypes, 14);
      expect(s.devices, 6);
      expect(s.lastFrameAgeMs, 12);
      expect(s.uptimeSeconds, 11520);
      expect(s.chartApps, 1);
      expect(s.notDelivered, 3);
      expect(s.appBufferCapacity, 8192);
    });

    test('anything else is refused', () {
      expect(TerminalStats.parse('{}'), isNull);
      expect(TerminalStats.parse('not json'), isNull);
      expect(TerminalStats.parse(firmwareJson(state: 'sideways')), isNull);
      expect(
        TerminalStats.parse(firmwareJson().replaceFirst('"v":1', '"v":2')),
        isNull,
      );
    });

    test('no frame yet is a null age', () {
      final s = TerminalStats.parse(
        firmwareJson(state: 'waiting', age: 'null'),
      )!;
      expect(s.lastFrameAgeMs, isNull);
      expect(formatAge(s.lastFrameAgeMs), 'none yet');
    });

    test('each state reads as live, simulated or a problem', () {
      TerminalStats of(String src, String state) =>
          TerminalStats.parse(firmwareJson(src: src, state: state))!;
      expect(of('n2k', 'live').tone, Tone.good);
      expect(of('n2k', 'live').pill, 'Live · NMEA 2000');
      expect(of('sim', 'sim').tone, Tone.caution);
      expect(of('sim', 'sim').pill, 'Simulation');
      expect(of('sim', 'sim').simulated, isTrue);
      expect(of('n2k', 'quiet').tone, Tone.neutral);
      expect(of('n2k', 'quiet').pill, 'Waiting for boat data');
      expect(of('n2k', 'bus_off').pill, 'Bus fault');
      expect(of('n2k', 'opening').tone, Tone.neutral);
    });

    test('buffer use is a share of capacity', () {
      final s = TerminalStats.parse(firmwareJson())!;
      expect(s.bufferPeakShare, closeTo(37 / 2048, 1e-9));
      expect(s.appBufferPeakShare, closeTo(1229 / 8192, 1e-9));
    });

    test('numbers read as people write them', () {
      expect(formatCount(0), '0');
      expect(formatCount(999), '999');
      expect(formatCount(1234567), '1,234,567');
      expect(formatUptime(45), '45 s');
      expect(formatUptime(3720), '1 h 2 min');
      expect(formatUptime(200000), '2 d 7 h');
      expect(formatAge(12), '12 ms ago');
      expect(formatAge(4200), '4 s ago');
    });
  });

  group('boat data page', () {
    testWidgets('live data says so and counts no loss', (tester) async {
      await pumpPage(tester, TerminalStats.parse(firmwareJson())!);
      expect(find.text('Live · NMEA 2000'), findsOneWidget);
      expect(
        find.text('No frames lost since the terminal started'),
        findsOneWidget,
      );
      expect(find.text('1,234,567'), findsOneWidget);
      expect(find.text('Peak 37 of 2,048 frames · now 0'), findsOneWidget);
      expect(find.textContaining('SIMULATION badge'), findsNothing);
    });

    testWidgets('the simulator is never mistaken for the boat', (tester) async {
      await pumpPage(
        tester,
        TerminalStats.parse(firmwareJson(src: 'sim', state: 'sim'))!,
      );
      // No big panel: a Simulation pill that explains itself when tapped.
      expect(find.text('Simulation'), findsOneWidget);
      expect(find.textContaining('SIMULATION badge'), findsNothing);
      await tester.tap(find.byKey(const ValueKey('data-source-pill')));
      await tester.pumpAndSettle();
      expect(find.textContaining('SIMULATION badge'), findsOneWidget);
    });

    testWidgets('lost frames are counted out loud', (tester) async {
      await pumpPage(
        tester,
        TerminalStats.parse(firmwareJson(lost: 12, lostQ: 12))!,
      );
      expect(
        find.text('12 frames lost since the terminal started'),
        findsOneWidget,
      );
    });

    testWidgets('the status pill names a quiet bus', (tester) async {
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: DataStatusPill(
              stats: TerminalStats.parse(firmwareJson(state: 'quiet'))!,
            ),
          ),
        ),
      );
      expect(
        find.text(TerminalStats.parse(firmwareJson(state: 'quiet'))!.pill),
        findsOneWidget,
      );
    });
  });
}
