import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lmyc_app/app_settings.dart';
import 'package:lmyc_app/instruments.dart';
import 'package:lmyc_app/main.dart' show lmycTheme;
import 'package:lmyc_app/settings_page.dart';
import 'package:shared_preferences/shared_preferences.dart';

Widget _page(AppSettings s, {Future<void> Function()? onForget}) => MaterialApp(
  theme: lmycTheme(Brightness.light),
  home: SettingsPage(
    settings: s,
    savedBoat: 'BENCH-01',
    onForget: onForget,
    terminalId: 'T-0001',
    version: '0.3.0 (3)',
  ),
);

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  group('AppSettings', () {
    test('defaults match the app before Settings existed', () {
      final s = AppSettings();
      expect(s.autoJoin, isTrue);
      expect(s.countdown, const Duration(seconds: 5));
      expect(s.arrivalSplash, isTrue);
      expect(s.startTab, StartTab.boat);
      expect(s.speedUnit, SpeedUnit.knots);
      expect(s.depthUnit, DepthUnit.metres);
      expect(s.keepScreenOn, isFalse);
      expect(s.themeMode, ThemeMode.system);
    });

    test('changes are kept for the next launch', () async {
      final s = AppSettings();
      await s.setAutoJoin(false);
      await s.setCountdownSeconds(10);
      await s.setArrivalSplash(false);
      await s.setStartTab(StartTab.instruments);
      await s.setSpeedUnit(SpeedUnit.metresPerSecond);
      await s.setDepthUnit(DepthUnit.feet);
      await s.setKeepScreenOn(true);
      await s.setThemeMode(ThemeMode.dark);
      final next = AppSettings();
      await next.load();
      expect(next.autoJoin, isFalse);
      expect(next.countdownSeconds, 10);
      expect(next.arrivalSplash, isFalse);
      expect(next.startTab, StartTab.instruments);
      expect(next.speedUnit, SpeedUnit.metresPerSecond);
      expect(next.depthUnit, DepthUnit.feet);
      expect(next.keepScreenOn, isTrue);
      expect(next.themeMode, ThemeMode.dark);
    });

    test('junk in storage falls back to defaults', () async {
      SharedPreferences.setMockInitialValues({
        'settings.countdown_s': 99,
        'settings.theme': 'purple',
      });
      final s = AppSettings();
      await s.load();
      expect(s.countdownSeconds, 5);
      expect(s.themeMode, ThemeMode.system);
    });

    test('unit conversion', () {
      final s = AppSettings();
      expect(s.speed(10), 10);
      expect(s.depth(2), 2);
      s.speedUnit = SpeedUnit.metresPerSecond;
      s.depthUnit = DepthUnit.feet;
      expect(s.speed(10), closeTo(5.14444, 1e-6));
      expect(s.depth(2), closeTo(6.56168, 1e-5));
      expect(s.speed(null), isNull);
    });
  });

  group('Settings page', () {
    testWidgets('a toggle applies at once', (tester) async {
      final s = AppSettings();
      await tester.pumpWidget(_page(s));
      expect(find.byKey(const ValueKey('settings-countdown')), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('settings-auto-join')));
      await tester.pump();
      expect(s.autoJoin, isFalse);
      expect(find.byKey(const ValueKey('settings-countdown')), findsNothing);
      expect(find.text('Always show the list of boats'), findsOneWidget);
    });

    testWidgets('choices apply at once', (tester) async {
      final s = AppSettings();
      await tester.pumpWidget(_page(s));
      await tester.tap(find.byKey(const ValueKey('settings-countdown-10 s')));
      await tester.pump();
      expect(s.countdownSeconds, 10);
      final feet = find.byKey(const ValueKey('settings-depth-Feet'));
      await tester.scrollUntilVisible(feet, 200);
      await tester.tap(feet);
      await tester.pump();
      expect(s.depthUnit, DepthUnit.feet);
      final dark = find.byKey(const ValueKey('settings-theme-Dark'));
      await tester.scrollUntilVisible(dark, 200);
      await tester.tap(dark);
      await tester.pump();
      expect(s.themeMode, ThemeMode.dark);
    });

    testWidgets('forget asks first', (tester) async {
      var forgot = 0;
      await tester.pumpWidget(
        _page(AppSettings(), onForget: () async => forgot++),
      );
      await tester.tap(find.byKey(const ValueKey('settings-forget')));
      await tester.pumpAndSettle();
      expect(find.text('Forget BENCH-01?'), findsOneWidget);
      await tester.tap(find.text('Cancel'));
      await tester.pumpAndSettle();
      expect(forgot, 0);
      await tester.tap(find.byKey(const ValueKey('settings-forget')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('settings-forget-confirm')));
      await tester.pumpAndSettle();
      expect(forgot, 1);
    });

    testWidgets('About shows the version and terminal ID', (tester) async {
      await tester.pumpWidget(_page(AppSettings()));
      final id = find.byKey(const ValueKey('settings-terminal-id'));
      await tester.scrollUntilVisible(id, 300);
      expect(find.text('0.3.0 (3)'), findsOneWidget);
      expect(find.text('T-0001'), findsOneWidget);
    });
  });

  testWidgets('Instruments follow the unit settings live', (tester) async {
    final s = AppSettings();
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: InstrumentsView(
            settings: s,
            lines: () => Stream.fromIterable([
              r'$SDDPT,2.0,0.0*00',
              r'$GPVTG,0,T,,M,10.0,N,18.5,K*00',
            ]),
          ),
        ),
      ),
    );
    await tester.pump();
    expect(find.text('metres'), findsOneWidget);
    expect(find.text('knots SOG'), findsOneWidget);
    s.depthUnit = DepthUnit.feet;
    s.speedUnit = SpeedUnit.metresPerSecond;
    s.notifyListeners();
    await tester.pump();
    expect(find.text('feet'), findsOneWidget);
    expect(find.text('m/s SOG'), findsOneWidget);
  });
}
