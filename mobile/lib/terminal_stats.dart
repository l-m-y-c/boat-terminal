import 'dart:convert';

/// Where the terminal's instrument data comes from.
enum DataMode { simulator, nmea2000 }

/// The receive path as the terminal reports it.
enum BusState {
  simulated,
  paused,
  opening,
  waiting,
  live,
  quiet,
  busOff,
  failed,
}

/// How a state should read: good is live data, caution is simulated,
/// bad is a bus that should be talking and is not.
enum Tone { good, caution, bad, neutral }

/// The JSON on BLE `…000050` (and `data` in GET /status.json).
/// Field names are the contract with firmware/src/terminal_stats.cpp.
class TerminalStats {
  const TerminalStats({
    required this.mode,
    required this.state,
    required this.framesPerSecond,
    required this.framesPerSecondPeak,
    required this.messagesPerSecond,
    required this.frames,
    required this.messages,
    required this.bufferCapacity,
    required this.bufferNow,
    required this.bufferPeak,
    required this.lost,
    required this.busErrors,
    required this.receiveErrorCounter,
    required this.pgnTypes,
    required this.devices,
    required this.lastFrameAgeMs,
    required this.uptimeSeconds,
    required this.chartApps,
    required this.sentencesOut,
    required this.notDelivered,
    required this.appBufferPeak,
    required this.appBufferCapacity,
  });

  final DataMode mode;
  final BusState state;
  final int framesPerSecond;
  final int framesPerSecondPeak;
  final int messagesPerSecond;
  final int frames;
  final int messages;
  final int bufferCapacity;
  final int bufferNow;
  final int bufferPeak;

  /// Frames that never reached the decoder: receive queue, driver queue and
  /// controller FIFO together.
  final int lost;
  final int busErrors;
  final int receiveErrorCounter;
  final int pgnTypes;
  final int devices;

  /// Null before the first frame.
  final int? lastFrameAgeMs;
  final int uptimeSeconds;
  final int chartApps;
  final int sentencesOut;

  /// Sentences a connected chart app did not get, plus failed broadcasts.
  final int notDelivered;
  final int appBufferPeak;
  final int appBufferCapacity;

  static const _states = {
    'sim': BusState.simulated,
    'paused': BusState.paused,
    'opening': BusState.opening,
    'waiting': BusState.waiting,
    'live': BusState.live,
    'quiet': BusState.quiet,
    'bus_off': BusState.busOff,
    'failed': BusState.failed,
  };

  /// Null for anything that is not a version 1 stats object.
  static TerminalStats? parse(String raw) {
    final cut = raw.split('\u0000').first.trim();
    Object? decoded;
    try {
      decoded = jsonDecode(cut);
    } catch (_) {
      return null;
    }
    if (decoded is! Map || decoded['v'] != 1) {
      return null;
    }
    final src = decoded['src'];
    final state = _states[decoded['state']];
    if ((src != 'sim' && src != 'n2k') || state == null) {
      return null;
    }
    int n(String key) {
      final v = decoded is Map ? decoded[key] : null;
      return v is int ? v : 0;
    }

    final age = decoded['age_ms'];
    return TerminalStats(
      mode: src == 'sim' ? DataMode.simulator : DataMode.nmea2000,
      state: state,
      framesPerSecond: n('fps'),
      framesPerSecondPeak: n('fps_peak'),
      messagesPerSecond: n('mps'),
      frames: n('frames'),
      messages: n('msgs'),
      bufferCapacity: n('q_cap'),
      bufferNow: n('q_now'),
      bufferPeak: n('q_peak'),
      lost: n('lost'),
      busErrors: n('bus_err'),
      receiveErrorCounter: n('rec'),
      pgnTypes: n('pgns'),
      devices: n('devices'),
      lastFrameAgeMs: age is int ? age : null,
      uptimeSeconds: n('up_s'),
      chartApps: n('apps'),
      sentencesOut: n('out'),
      notDelivered: n('out_lost') + n('udp_lost'),
      appBufferPeak: n('out_peak'),
      appBufferCapacity: n('out_cap'),
    );
  }

  bool get simulated => mode == DataMode.simulator;

  Tone get tone => switch (state) {
    BusState.live => Tone.good,
    BusState.simulated || BusState.paused => Tone.caution,
    BusState.opening => Tone.neutral,
    BusState.waiting ||
    BusState.quiet ||
    BusState.busOff ||
    BusState.failed => Tone.bad,
  };

  /// The few words on the status pill.
  String get pill => switch (state) {
    BusState.live => 'Live · NMEA 2000',
    BusState.simulated => 'Simulation',
    BusState.paused => 'Simulation paused',
    BusState.opening => 'Starting',
    BusState.waiting || BusState.quiet => 'No bus data',
    BusState.busOff || BusState.failed => 'Bus fault',
  };

  /// One sentence under the headline.
  String get summary => switch (state) {
    BusState.live =>
      'Live from the boat\'s NMEA 2000 network. '
          '$messagesPerSecond messages a second from $devices '
          '${devices == 1 ? 'device' : 'devices'}.',
    BusState.simulated =>
      'The terminal is simulating a boat. This is not the boat\'s data.',
    BusState.paused =>
      'The simulator is paused for a bench test. This is not the boat\'s data.',
    BusState.opening => 'The terminal is starting its NMEA 2000 interface.',
    BusState.waiting =>
      'Listening to NMEA 2000, but nothing has arrived. '
          'Check the drop cable and that the instruments are on.',
    BusState.quiet =>
      'The NMEA 2000 network has gone quiet. Values on the terminal are '
          'blank rather than out of date.',
    BusState.busOff =>
      'The terminal dropped off the bus after a fault and is retrying. '
          'It never transmits.',
    BusState.failed => 'The terminal\'s CAN interface did not start.',
  };

  /// Share of the receive buffer used at its fullest, 0 to 1.
  double get bufferPeakShare =>
      bufferCapacity == 0 ? 0 : (bufferPeak / bufferCapacity).clamp(0.0, 1.0);

  double get appBufferPeakShare => appBufferCapacity == 0
      ? 0
      : (appBufferPeak / appBufferCapacity).clamp(0.0, 1.0);
}

/// 1234567 -> "1,234,567".
String formatCount(int value) {
  final digits = value.abs().toString();
  final out = StringBuffer(value < 0 ? '-' : '');
  for (var i = 0; i < digits.length; i++) {
    if (i > 0 && (digits.length - i) % 3 == 0) out.write(',');
    out.write(digits[i]);
  }
  return out.toString();
}

/// 45 -> "45 s", 3720 -> "1 h 2 min", 200000 -> "2 d 7 h".
String formatUptime(int seconds) {
  if (seconds < 60) return '$seconds s';
  final minutes = seconds ~/ 60;
  if (minutes < 60) return '$minutes min';
  final hours = minutes ~/ 60;
  if (hours < 24) return '$hours h ${minutes % 60} min';
  return '${hours ~/ 24} d ${hours % 24} h';
}

/// "12 ms ago", "4 s ago", or "none yet".
String formatAge(int? ms) {
  if (ms == null) return 'none yet';
  if (ms < 1000) return '$ms ms ago';
  return '${ms ~/ 1000} s ago';
}
