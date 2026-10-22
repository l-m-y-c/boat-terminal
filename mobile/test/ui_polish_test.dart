import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lmyc_app/counter_help.dart';
import 'package:lmyc_app/instruments.dart';
import 'package:lmyc_app/journey.dart';
import 'package:lmyc_app/nmea_data_page.dart';
import 'package:lmyc_app/nmea_sentences.dart';
import 'package:lmyc_app/terminal_stats.dart';
import 'package:lmyc_app/ui_kit.dart';

const _json =
    '{"v":1,"src":"n2k","state":"live","fps":312,"fps_peak":540,"mps":120,'
    '"frames":1234567,"msgs":45678,"q_cap":2048,"q_now":0,"q_peak":37,'
    '"lost":0,"lost_q":0,"lost_drv":0,"lost_hw":0,"bus_err":0,"rec":0,'
    '"pgns":14,"devices":6,"age_ms":12,"up_s":11520,'
    '"apps":1,"out":5678,"out_lost":2,"udp_lost":1,"out_peak":1229,"out_cap":8192}';

Future<void> _tall(WidgetTester tester) async {
  await tester.binding.setSurfaceSize(const Size(420, 2200));
  addTearDown(() => tester.binding.setSurfaceSize(null));
}

void main() {
  group('simulation badge', () {
    testWidgets('sits on the boat title and opens the explanation', (
      tester,
    ) async {
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            appBar: AppBar(
              title: const BoatTitle(name: 'BENCH-01', simulated: true),
            ),
          ),
        ),
      );
      expect(find.text('BENCH-01'), findsOneWidget);
      expect(find.text('Simulation'), findsOneWidget);
      expect(find.textContaining('simulating a boat'), findsNothing);

      await tester.tap(find.byKey(const ValueKey('simulation-badge')));
      await tester.pumpAndSettle();
      expect(find.textContaining('simulating a boat'), findsOneWidget);
      expect(find.textContaining('SIMULATION badge'), findsOneWidget);
    });

    testWidgets('is absent when the data is real', (tester) async {
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(appBar: AppBar(title: const BoatTitle(name: 'X'))),
        ),
      );
      expect(find.byType(SimulationBadge), findsNothing);
    });
  });

  group('NMEA data help', () {
    testWidgets('every counter panel opens its own help', (tester) async {
      await _tall(tester);
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: NmeaDataView(
              stats: TerminalStats.parse(_json),
              updatedAt: null,
              supported: true,
            ),
          ),
        ),
      );
      for (final entry in counterHelp.entries) {
        final panel = find.byKey(ValueKey('metric-${entry.key}'));
        expect(panel, findsOneWidget, reason: entry.key);
        await tester.tap(panel);
        await tester.pumpAndSettle();
        expect(
          find.text(entry.value.paragraphs.first),
          findsOneWidget,
          reason: entry.key,
        );
        Navigator.of(tester.element(find.text(entry.value.title).last)).pop();
        await tester.pumpAndSettle();
      }
    });

    testWidgets('Not delivered explains both of its parts', (tester) async {
      await _tall(tester);
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: NmeaDataView(
              stats: TerminalStats.parse(_json),
              updatedAt: null,
              supported: true,
            ),
          ),
        ),
      );
      await tester.tap(find.byKey(const ValueKey('metric-not_delivered')));
      await tester.pumpAndSettle();
      expect(find.textContaining('8 KB queue'), findsWidgets);
      expect(find.textContaining('UDP broadcast failed'), findsOneWidget);
    });
  });

  group('instruments', () {
    test('decodes like the terminal and ages values out', () {
      var now = DateTime(2026, 10, 22, 12);
      final r = InstrumentReadings(clock: () => now);
      expect(r.depthM, isNull);
      r.note(nmeaSentence('SDDPT,4.2,0.0'));
      r.note(nmeaSentence('GPRMC,120000.00,A,4916.0,N,12311.0,W,5.4,231.0,221026,,,A'));
      r.note(nmeaSentence('WIMWV,315.0,R,12.0,N,A'));
      expect(r.depthM, 4.2);
      expect(r.sogKn, 5.4);
      expect(r.cogDeg, 231.0);
      expect(r.awsKn, 12.0);
      expect(r.awaDeg, -45.0);
      r.note(nmeaSentence('IIMWV,90.0,T,20.0,N,A')); // true wind: ignored
      expect(r.awsKn, 12.0);
      r.note(nmeaSentence('IIMWV,30.0,R,5.0,M,A')); // m/s -> knots
      expect(r.awsKn, closeTo(9.72, 0.01));
      expect(r.note(r'$SDDPT,9.9,0.0*00'), isFalse); // bad checksum
      now = now.add(const Duration(seconds: 6));
      expect(r.depthM, isNull);
      expect(r.anyFresh, isFalse);
    });

    testWidgets('shows a dash for every missing value', (tester) async {
      final lines = StreamController<String>();
      addTearDown(lines.close);
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(body: InstrumentsView(lines: () => lines.stream)),
        ),
      );
      for (final c in ['DEPTH', 'SPEED', 'COURSE', 'WIND']) {
        expect(find.text(c), findsOneWidget);
      }
      expect(find.text('—'), findsNWidgets(4));
      expect(find.textContaining('Waiting for depth'), findsOneWidget);

      lines.add(nmeaSentence('SDDPT,3.4,0.0'));
      await tester.pump();
      expect(find.text('3.4'), findsOneWidget);
      expect(find.text('—'), findsNWidgets(3));
    });
  });

  testWidgets('the journey strip keeps four ticks when compact', (
    tester,
  ) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: JourneyBar(current: JourneyStop.arrived, compact: true),
        ),
      ),
    );
    expect(find.byIcon(Icons.check), findsNWidgets(4));
  });
}
