import 'package:flutter/foundation.dart';

/// One line in the app's recent history.
class LogEvent {
  const LogEvent(this.at, this.text);

  final DateTime at;
  final String text;
}

/// The last few hundred things the app noticed: Bluetooth steps, Wi-Fi and
/// relay changes, errors. Kept in memory only, never written to disk, and
/// shown under Diagnostics and in the shared report.
class EventLog extends ChangeNotifier {
  EventLog({this.capacity = 200, DateTime Function()? clock})
    : _clock = clock ?? DateTime.now;

  /// The app's one log.
  static final instance = EventLog();

  final int capacity;
  final DateTime Function() _clock;
  final List<LogEvent> _events = [];

  /// Oldest first.
  List<LogEvent> get events => List.unmodifiable(_events);

  void add(String text) {
    final line = text.trim();
    if (line.isEmpty) return;
    _events.add(LogEvent(_clock(), line));
    if (_events.length > capacity) {
      _events.removeRange(0, _events.length - capacity);
    }
    notifyListeners();
  }

  void clear() {
    _events.clear();
    notifyListeners();
  }

  /// Copies everything the app prints with [debugPrint] into [log] as well,
  /// so the existing traces show up without touching the connection code.
  static void captureDebugPrint([EventLog? log]) {
    final target = log ?? instance;
    final original = debugPrint;
    debugPrint = (String? message, {int? wrapWidth}) {
      if (message != null) target.add(message);
      original(message, wrapWidth: wrapWidth);
    };
  }
}

/// `14:03:27`
String clockTime(DateTime t) =>
    '${t.hour.toString().padLeft(2, '0')}:'
    '${t.minute.toString().padLeft(2, '0')}:'
    '${t.second.toString().padLeft(2, '0')}';
