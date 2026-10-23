import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;

import 'package:flutter/material.dart';

import 'app_settings.dart';
import 'boat_wifi.dart';
import 'journey.dart';
import 'nmea_sentences.dart';
import 'ui_kit.dart';

/// A value older than this is shown as missing, never as current. The same
/// five seconds as kNmeaInstrumentStaleMs in firmware/src/nmea_state.h.
const kInstrumentStale = Duration(seconds: 5);

/// Depth, speed, course and wind from NMEA 0183, decoded the way the
/// terminal's own Instruments screen does it (firmware/src/nmea_state.cpp):
/// RMC and VTG for SOG/COG, DPT and DBT for depth, MWV for wind.
class InstrumentReadings {
  InstrumentReadings({DateTime Function()? clock})
    : _clock = clock ?? DateTime.now;

  final DateTime Function() _clock;

  double? _depthM;
  double? _sogKn;
  double? _cogDeg;
  double? _awsKn;
  double? _awaDeg;
  DateTime? _depthAt;
  DateTime? _sogAt;
  DateTime? _cogAt;
  DateTime? _windAt;
  DateTime? _awaAt;

  bool _fresh(DateTime? at) =>
      at != null && _clock().difference(at) <= kInstrumentStale;

  double? get depthM => _fresh(_depthAt) ? _depthM : null;
  double? get sogKn => _fresh(_sogAt) ? _sogKn : null;
  double? get cogDeg => _fresh(_cogAt) ? _cogDeg : null;
  double? get awsKn => _fresh(_windAt) ? _awsKn : null;

  /// Apparent wind angle, -180..180, positive to starboard.
  double? get awaDeg => _fresh(_awaAt) ? _awaDeg : null;

  bool get anyFresh =>
      depthM != null || sogKn != null || cogDeg != null || awsKn != null;

  /// Feed one sentence. Returns true when it changed a reading.
  bool note(String raw) {
    final line = raw.trim();
    if (line.length < 6 || (line[0] != r'$' && line[0] != '!')) return false;
    if (line.contains('*') && !nmeaChecksumOk(line)) return false;
    final star = line.indexOf('*');
    final fields = (star < 0 ? line : line.substring(0, star)).split(',');
    final formatter = line.substring(3, 6);
    final now = _clock();
    double? f(int i) => i < fields.length ? double.tryParse(fields[i]) : null;
    var changed = false;
    void sog(double? v) {
      if (v == null) return;
      _sogKn = v;
      _sogAt = now;
      changed = true;
    }

    void cog(double? v) {
      if (v == null) return;
      _cogDeg = v;
      _cogAt = now;
      changed = true;
    }

    void depth(double? v) {
      if (v == null) return;
      _depthM = v;
      _depthAt = now;
      changed = true;
    }

    switch (formatter) {
      case 'RMC' when fields.length > 8:
        sog(f(7));
        cog(f(8));
      case 'VTG' when fields.length > 5:
        // VTG,cog,T,,M,sog,N,kmh,K: 129026 with no position sends VTG only.
        sog(f(5));
        cog(f(1));
      case 'DBT' when fields.length > 3:
        depth(f(3)); // feet, metres, fathoms: field 3 is metres
      case 'DPT' when fields.length > 1:
        depth(f(1));
      case 'MWV' when fields.length > 4:
        var angle = f(1);
        final speed = f(3);
        if (angle == null || speed == null) break;
        // Only apparent (R) wind: the tile is AWS / AWA, like the terminal.
        if (fields[2].toUpperCase() != 'R') break;
        final kn = switch (fields[4].toUpperCase()) {
          'M' => speed * 1.943844,
          'K' => speed / 1.852,
          _ => speed,
        };
        if (angle > 180) angle -= 360;
        _awsKn = kn;
        _awaDeg = angle;
        _windAt = now;
        _awaAt = now;
        changed = true;
    }
    return changed;
  }
}

/// Lines of NMEA from the phone's relay on 127.0.0.1:10110: the same feed
/// chart apps get. Reconnects every few seconds while listened to.
Stream<String> relayLines() => _RelayLines().stream;

class _RelayLines {
  _RelayLines() {
    _out = StreamController<String>(
      onListen: () => unawaited(_connect()),
      onCancel: _close,
    );
  }

  late final StreamController<String> _out;
  Socket? _socket;
  Timer? _retry;
  bool _closed = false;

  Stream<String> get stream => _out.stream;

  Future<void> _connect() async {
    if (_closed) return;
    try {
      final s = await Socket.connect(
        kRelayHost,
        10110,
        timeout: const Duration(seconds: 3),
      );
      if (_closed) {
        s.destroy();
        return;
      }
      _socket = s;
      s
          .cast<List<int>>()
          .transform(const Utf8Decoder(allowMalformed: true))
          .transform(const LineSplitter())
          .listen(
            _out.add,
            onDone: _again,
            onError: (Object _) => _again(),
            cancelOnError: true,
          );
    } catch (_) {
      _again();
    }
  }

  void _again() {
    _socket?.destroy();
    _socket = null;
    if (_closed) return;
    _retry?.cancel();
    _retry = Timer(const Duration(seconds: 3), () => unawaited(_connect()));
  }

  void _close() {
    _closed = true;
    _retry?.cancel();
    _socket?.destroy();
  }
}

/// Big, glanceable depth, speed, course and wind, mirroring the terminal's
/// Instruments screen. A value with nothing recent reads "—".
class InstrumentsView extends StatefulWidget {
  const InstrumentsView({super.key, this.lines, this.clock, this.settings});

  /// Units come from here. Defaults to [AppSettings.instance].
  final AppSettings? settings;

  /// Where sentences come from. Defaults to the phone's relay.
  final Stream<String> Function()? lines;
  final DateTime Function()? clock;

  @override
  State<InstrumentsView> createState() => _InstrumentsViewState();
}

class _InstrumentsViewState extends State<InstrumentsView> {
  late final InstrumentReadings _readings = InstrumentReadings(
    clock: widget.clock,
  );
  StreamSubscription<String>? _sub;
  Timer? _tick;

  AppSettings get _settings => widget.settings ?? AppSettings.instance;

  void _changed() {
    if (mounted) setState(() {});
  }

  @override
  void initState() {
    super.initState();
    _settings.addListener(_changed);
    _sub = (widget.lines ?? relayLines)().listen((line) {
      if (_readings.note(line) && mounted) setState(() {});
    });
    // Values age out even when nothing arrives.
    _tick = Timer.periodic(const Duration(seconds: 1), (_) {
      if (mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    _settings.removeListener(_changed);
    _tick?.cancel();
    unawaited(_sub?.cancel());
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final r = _readings;
    final awa = r.awaDeg;
    final u = _settings;
    final ms = u.speedUnit == SpeedUnit.metresPerSecond;
    final tiles = [
      _Gauge(
        caption: 'DEPTH',
        unit: u.depthLabel,
        value: _fmt(u.depth(r.depthM), 1),
        icon: Icons.waves,
      ),
      _Gauge(
        caption: 'SPEED',
        unit: ms ? 'm/s SOG' : 'knots SOG',
        value: _fmt(u.speed(r.sogKn), 1),
        icon: Icons.speed,
      ),
      _Gauge(
        caption: 'COURSE',
        unit: 'degrees COG',
        value: _fmt(r.cogDeg, 0),
        pointer: r.cogDeg,
      ),
      _Gauge(
        caption: 'WIND',
        unit: awa == null
            ? '${u.speedLabel} AWS · apparent'
            : '${u.speedLabel} AWS · ${awa >= 0 ? '+' : ''}${awa.round()}° AWA',
        value: _fmt(u.speed(r.awsKn), ms ? 1 : 0),
        icon: Icons.air,
      ),
    ];
    return LayoutBuilder(
      builder: (context, box) {
        const gap = 12.0;
        final columns = box.maxWidth >= 640 ? 4 : 2;
        final width = (box.maxWidth - 40 - gap * (columns - 1)) / columns;
        return ListView(
          padding: const EdgeInsets.fromLTRB(20, 12, 20, 24),
          children: [
            Wrap(
              spacing: gap,
              runSpacing: gap,
              children: [
                for (final t in tiles) SizedBox(width: width, child: t),
              ],
            ),
            SizedBox(height: 16),
            Text(
              r.anyFresh
                  ? 'From the terminal over the boat Wi-Fi. A value with '
                        'nothing new for 5 s shows —.'
                  : 'Waiting for depth, speed, course and wind. A value '
                        'with nothing new for 5 s shows —.',
              style: TextStyle(color: Palette.of(context).faint, fontSize: 13),
            ),
          ],
        );
      },
    );
  }
}

String _fmt(double? v, int decimals) =>
    v == null ? '—' : v.toStringAsFixed(decimals);

class _Gauge extends StatelessWidget {
  const _Gauge({
    required this.caption,
    required this.unit,
    required this.value,
    this.icon,
    this.pointer,
  });

  final String caption;
  final String unit;
  final String value;
  final IconData? icon;

  /// Degrees for a small direction arrow, 0 = up.
  final double? pointer;

  @override
  Widget build(BuildContext context) {
    final missing = value == '—';
    return Container(
      key: ValueKey('gauge-$caption'),
      height: 150,
      padding: EdgeInsets.fromLTRB(16, 14, 16, 14),
      decoration: BoxDecoration(
        color: kNavy,
        borderRadius: BorderRadius.circular(18),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Text(
                caption,
                style: const TextStyle(
                  color: Color(0xFF8FB3D9),
                  fontWeight: FontWeight.w700,
                  letterSpacing: 1.2,
                  fontSize: 12.5,
                ),
              ),
              const Spacer(),
              if (pointer != null)
                Transform.rotate(
                  angle: pointer! * math.pi / 180,
                  child: const Icon(
                    Icons.navigation,
                    size: 18,
                    color: Color(0xFF00B4A6),
                  ),
                )
              else if (icon != null)
                Icon(icon, size: 18, color: const Color(0xFF3A5A7C)),
            ],
          ),
          Expanded(
            child: Align(
              alignment: Alignment.centerLeft,
              child: FittedBox(
                fit: BoxFit.scaleDown,
                child: Text(
                  value,
                  style: TextStyle(
                    color: missing ? const Color(0xFF5C7A99) : Colors.white,
                    fontSize: 48,
                    fontWeight: FontWeight.w700,
                    fontFeatures: const [FontFeature.tabularFigures()],
                  ),
                ),
              ),
            ),
          ),
          Text(
            unit,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(color: Color(0xFF8FB3D9), fontSize: 12.5),
          ),
        ],
      ),
    );
  }
}
