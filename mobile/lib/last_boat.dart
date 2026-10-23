import 'package:shared_preferences/shared_preferences.dart';

/// The terminal this phone joined last, so the next launch can rejoin it.
class LastBoat {
  static const _key = 'last_terminal_ble_name';

  static Future<String?> load() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      return prefs.getString(_key);
    } catch (_) {
      return null;
    }
  }

  static Future<void> save(String bleName) async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setString(_key, bleName);
    } catch (_) {}
  }

  static Future<void> forget() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.remove(_key);
    } catch (_) {}
  }
}
