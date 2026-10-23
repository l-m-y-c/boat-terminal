// Large system text (Android "Font size" at its top step is about 1.6–2x)
// must not overflow the screens a member reaches before and after joining.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lmyc_app/app_settings.dart';
import 'package:lmyc_app/diagnostics.dart';
import 'package:lmyc_app/event_log.dart';
import 'package:lmyc_app/instruments.dart';
import 'package:lmyc_app/main.dart' show lmycTheme;
import 'package:lmyc_app/settings_page.dart';

Widget _wrap(Widget child, double scale, Brightness b) => MaterialApp(
  theme: lmycTheme(b),
  home: MediaQuery(
    data: MediaQueryData(
      size: const Size(360, 780),
      textScaler: TextScaler.linear(scale),
    ),
    child: child,
  ),
);

void main() {
  for (final scale in [1.0, 1.6, 2.0]) {
    for (final b in Brightness.values) {
      testWidgets('Settings at ${scale}x ${b.name}', (tester) async {
        tester.view.physicalSize = const Size(1080, 2340);
        tester.view.devicePixelRatio = 3;
        addTearDown(tester.view.reset);
        await tester.pumpWidget(
          _wrap(
            SettingsPage(
              settings: AppSettings(),
              savedBoat: 'BENCH-01',
              onForget: () async {},
              terminalId: 'T-0001',
              onOpenDiagnostics: () {},
              version: '0.3.0 (3)',
            ),
            scale,
            b,
          ),
        );
        await tester.drag(find.byType(ListView), const Offset(0, -3000));
        await tester.pump();
        expect(tester.takeException(), isNull);
      });

      testWidgets('Diagnostics at ${scale}x ${b.name}', (tester) async {
        tester.view.physicalSize = const Size(1080, 2340);
        tester.view.devicePixelRatio = 3;
        addTearDown(tester.view.reset);
        final log = EventLog()..add('Bluetooth: Paired');
        await tester.pumpWidget(
          _wrap(
            Scaffold(
              body: DiagnosticsView(
                snapshot: DiagnosticsSnapshot(
                  takenAt: DateTime(2026, 10, 22),
                  boat: 'BENCH-01',
                  bleName: 'LMYC-BENCH-01-3F2A',
                  bleState: BleState.failed,
                  bleLabel: 'Could not connect',
                ),
                log: log,
                onReconnect: () {},
              ),
            ),
            scale,
            b,
          ),
        );
        await tester.drag(
          find.byKey(const ValueKey('diagnostics')),
          const Offset(0, -3000),
        );
        await tester.pump();
        expect(tester.takeException(), isNull);
      });

      testWidgets('Instruments at ${scale}x ${b.name}', (tester) async {
        tester.view.physicalSize = const Size(1080, 2340);
        tester.view.devicePixelRatio = 3;
        addTearDown(tester.view.reset);
        await tester.pumpWidget(
          _wrap(
            Scaffold(
              body: InstrumentsView(
                settings: AppSettings(),
                lines: () => const Stream.empty(),
              ),
            ),
            scale,
            b,
          ),
        );
        await tester.pump();
        expect(tester.takeException(), isNull);
      });
    }
  }
}
