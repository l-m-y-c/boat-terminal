import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

enum SpeedUnit { knots, metresPerSecond }

enum DepthUnit { metres, feet }

/// Which on-board tab opens after arrival. Mirrors OnBoardTab by name so
/// settings do not depend on main.dart.
enum StartTab { boat, instruments, nmeaData, network, diagnostics }

const kMsPerKnot = 0.514444;
const kFeetPerMetre = 3.28084;

/// The member's preferences, saved on the phone and applied as they change.
/// Nothing here goes to the terminal.
class AppSettings extends ChangeNotifier {
  AppSettings();

  static final instance = AppSettings();

  static const countdownChoices = [3, 5, 10];

  bool autoJoin = true;
  int countdownSeconds = 5;
  bool arrivalSplash = true;
  StartTab startTab = StartTab.boat;
  SpeedUnit speedUnit = SpeedUnit.knots;
  DepthUnit depthUnit = DepthUnit.metres;
  bool keepScreenOn = false;
  ThemeMode themeMode = ThemeMode.system;

  Duration get countdown => Duration(seconds: countdownSeconds);

  static const _p = 'settings.';

  Future<void> load() async {
    try {
      final s = await SharedPreferences.getInstance();
      autoJoin = s.getBool('${_p}auto_join') ?? autoJoin;
      final c = s.getInt('${_p}countdown_s');
      if (c != null && countdownChoices.contains(c)) countdownSeconds = c;
      arrivalSplash = s.getBool('${_p}splash') ?? arrivalSplash;
      startTab = _pick(
        StartTab.values,
        s.getString('${_p}start_tab'),
        startTab,
      );
      speedUnit = _pick(SpeedUnit.values, s.getString('${_p}speed'), speedUnit);
      depthUnit = _pick(DepthUnit.values, s.getString('${_p}depth'), depthUnit);
      keepScreenOn = s.getBool('${_p}keep_on') ?? keepScreenOn;
      themeMode = _pick(ThemeMode.values, s.getString('${_p}theme'), themeMode);
    } catch (e) {
      debugPrint('prefs: $e');
    }
    notifyListeners();
  }

  static T _pick<T extends Enum>(List<T> values, String? name, T fallback) {
    for (final v in values) {
      if (v.name == name) return v;
    }
    return fallback;
  }

  Future<void> _save(String key, Object value) async {
    notifyListeners();
    try {
      final s = await SharedPreferences.getInstance();
      switch (value) {
        case final bool b:
          await s.setBool('$_p$key', b);
        case final int i:
          await s.setInt('$_p$key', i);
        case final Enum e:
          await s.setString('$_p$key', e.name);
      }
    } catch (e) {
      debugPrint('prefs: $e');
    }
  }

  Future<void> setAutoJoin(bool v) {
    autoJoin = v;
    return _save('auto_join', v);
  }

  Future<void> setCountdownSeconds(int v) {
    countdownSeconds = v;
    return _save('countdown_s', v);
  }

  Future<void> setArrivalSplash(bool v) {
    arrivalSplash = v;
    return _save('splash', v);
  }

  Future<void> setStartTab(StartTab v) {
    startTab = v;
    return _save('start_tab', v);
  }

  Future<void> setSpeedUnit(SpeedUnit v) {
    speedUnit = v;
    return _save('speed', v);
  }

  Future<void> setDepthUnit(DepthUnit v) {
    depthUnit = v;
    return _save('depth', v);
  }

  Future<void> setKeepScreenOn(bool v) {
    keepScreenOn = v;
    return _save('keep_on', v);
  }

  Future<void> setThemeMode(ThemeMode v) {
    themeMode = v;
    return _save('theme', v);
  }

  /// Speed in the chosen unit, from knots.
  double? speed(double? kn) =>
      kn == null ? null : (speedUnit == SpeedUnit.knots ? kn : kn * kMsPerKnot);

  String get speedLabel => speedUnit == SpeedUnit.knots ? 'kn' : 'm/s';

  double? depth(double? m) => m == null
      ? null
      : (depthUnit == DepthUnit.metres ? m : m * kFeetPerMetre);

  String get depthLabel => depthUnit == DepthUnit.metres ? 'metres' : 'feet';
}
