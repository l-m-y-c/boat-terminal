/// Bus check: the terminal's NMEA 2000 picture for the first runs on a
/// boat (GET /n2k.json), and a raw recording of the bus (GET /n2k/frames)
/// that can be shared and replayed on the bench.
library;

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'package:share_plus/share_plus.dart';

import 'boat_wifi.dart';
import 'network_page.dart';
import 'nmea_data_page.dart' show toneColor;
import 'terminal_stats.dart';
import 'ui_kit.dart';

int _int(Object? v) => v is int ? v : (v is num ? v.toInt() : 0);
int? _intOrNull(Object? v) => v is num ? v.toInt() : null;
String _str(Object? v) => v is String ? v : '';

/// One device heard on the bus. Names come from its address claim (60928)
/// and product information (126996), when it sent them.
class DiagDevice {
  const DiagDevice({
    required this.src,
    required this.frames,
    required this.pgns,
    required this.ageMs,
    this.maker,
    this.manufacturerCode,
    this.className,
    this.model,
  });

  final int src;
  final int frames;
  final int pgns;
  final int ageMs;
  final String? maker;
  final int? manufacturerCode;
  final String? className;
  final String? model;

  /// "Garmin GPSMAP 740s", "Garmin (Display)", "Maker 1234", "Device 35".
  String get title {
    final who = (maker?.isNotEmpty ?? false)
        ? maker!
        : manufacturerCode != null
        ? 'Maker $manufacturerCode'
        : 'Device $src';
    if (model?.isNotEmpty ?? false) return '$who ${model!}';
    if (className?.isNotEmpty ?? false) return '$who ($className)';
    return who;
  }

  static DiagDevice fromJson(Map<String, Object?> j) => DiagDevice(
    src: _int(j['src']),
    frames: _int(j['frames']),
    pgns: _int(j['pgns']),
    ageMs: _int(j['age_ms']),
    maker: j['maker'] as String?,
    manufacturerCode: _intOrNull(j['mfr']),
    className: j['class_name'] as String?,
    model: j['model'] as String?,
  );
}

/// One PGN from one device, at frame and message level.
class DiagPgn {
  const DiagPgn({
    required this.pgn,
    required this.name,
    required this.src,
    required this.fast,
    required this.frames,
    required this.starts,
    required this.messages,
    required this.rate,
    required this.ageMs,
  });

  final int pgn;
  final String name;
  final int src;
  final bool fast;
  final int frames;

  /// Fast-packet first frames.
  final int starts;
  final int messages;
  final double rate;
  final int ageMs;

  /// Fast-packet messages that started and never came out whole.
  int get incomplete => fast && starts > messages ? starts - messages : 0;

  static DiagPgn fromJson(Map<String, Object?> j) => DiagPgn(
    pgn: _int(j['pgn']),
    name: _str(j['name']),
    src: _int(j['src']),
    fast: j['fast'] == true,
    frames: _int(j['frames']),
    starts: _int(j['starts']),
    messages: _int(j['msgs']),
    rate: (j['rate'] as num?)?.toDouble() ?? 0,
    ageMs: _int(j['age_ms']),
  );
}

/// What the converter did with one PGN.
class DiagConv {
  const DiagConv({
    required this.pgn,
    required this.received,
    required this.used,
    required this.notAvailable,
    required this.bad,
    required this.throttled,
    required this.otherSource,
  });

  final int pgn;
  final int received;
  final int used;
  final int notAvailable;
  final int bad;
  final int throttled;
  final int otherSource;

  static DiagConv fromJson(Map<String, Object?> j) => DiagConv(
    pgn: _int(j['pgn']),
    received: _int(j['rx']),
    used: _int(j['used']),
    notAvailable: _int(j['na']),
    bad: _int(j['bad']),
    throttled: _int(j['throttled']),
    otherSource: _int(j['other_src']),
  );
}

/// Which device a value is taken from, and who else sends it.
class DiagOwner {
  const DiagOwner({
    required this.what,
    required this.src,
    required this.ageMs,
    this.other,
  });

  final String what;
  final int src;
  final int ageMs;
  final int? other;

  static DiagOwner fromJson(Map<String, Object?> j) => DiagOwner(
    what: _str(j['what']),
    src: _int(j['src']),
    ageMs: _int(j['age_ms']),
    other: _intOrNull(j['other']),
  );

  String get label => switch (what) {
    'cog_sog' => 'Course & speed',
    'water_speed' => 'Speed through water',
    'wind_apparent' => 'Apparent wind',
    'wind_true' => 'True wind',
    'wind_ground' => 'Wind direction',
    'sea_temp' => 'Sea temperature',
    _ => what.isEmpty ? '?' : '${what[0].toUpperCase()}${what.substring(1)}',
  };
}

/// GET /n2k.json, parsed. Field names are the contract with
/// firmware/src/n2k_diag.cpp.
class BusDiag {
  const BusDiag({
    required this.raw,
    required this.source,
    required this.state,
    required this.node,
    required this.address,
    required this.txFrames,
    required this.hint,
    required this.hintText,
    required this.framesPerSecond,
    required this.frames,
    required this.lost,
    required this.busErrors,
    required this.repeats,
    required this.repeatsPerSecond,
    required this.fastIncomplete,
    required this.badValues,
    required this.devices,
    required this.pgns,
    required this.conv,
    required this.owners,
  });

  final String raw;
  final String source;
  final String state;

  /// This terminal as a node: "claimed", "claiming", "no_address", "off".
  final String node;

  /// Its source address, 254 for none.
  final int address;

  /// Network-management frames it has sent.
  final int txFrames;
  final String hint;
  final String hintText;
  final int framesPerSecond;
  final int frames;
  final int lost;
  final int busErrors;
  final int repeats;
  final int repeatsPerSecond;
  final int fastIncomplete;
  final int badValues;
  final List<DiagDevice> devices;
  final List<DiagPgn> pgns;
  final List<DiagConv> conv;
  final List<DiagOwner> owners;

  /// Null for anything that is not a version 1 /n2k.json with a bus.
  static BusDiag? parse(String body) {
    Object? d;
    try {
      d = jsonDecode(body);
    } on FormatException {
      return null;
    }
    if (d is! Map || d['v'] != 1 || d['bus'] is! Map) return null;
    final b = (d['bus'] as Map).cast<String, Object?>();
    final self = d['self'] is Map
        ? (d['self'] as Map).cast<String, Object?>()
        : const <String, Object?>{};
    List<T> list<T>(String key, T Function(Map<String, Object?>) f) {
      final v = d is Map ? d[key] : null;
      if (v is! List) return const [];
      return [
        for (final e in v)
          if (e is Map) f(e.cast<String, Object?>()),
      ];
    }

    return BusDiag(
      raw: body,
      source: _str(b['src']),
      state: _str(b['state']),
      node: _str(self['node']).isEmpty ? 'off' : _str(self['node']),
      address: self.containsKey('address') ? _int(self['address']) : 254,
      txFrames: _int(self['tx_frames']),
      hint: _str(b['hint']).isEmpty ? 'ok' : _str(b['hint']),
      hintText: _str(b['hint_text']),
      framesPerSecond: _int(b['fps']),
      frames: _int(b['frames']),
      lost: _int(b['lost_q']) + _int(b['lost_drv']) + _int(b['lost_hw']),
      busErrors: _int(b['bus_err']),
      repeats: _int(b['repeats']),
      repeatsPerSecond: _int(b['repeats_ps']),
      fastIncomplete: _int(b['fast_incomplete']),
      badValues: _int(b['bad_values']),
      devices: list('devices', DiagDevice.fromJson),
      pgns: list('pgns', DiagPgn.fromJson)
        ..sort((a, b) => a.pgn != b.pgn ? a.pgn - b.pgn : a.src - b.src),
      conv: list('conv', DiagConv.fromJson),
      owners: list('sources', DiagOwner.fromJson),
    );
  }

  String pgnName(int pgn) {
    for (final p in pgns) {
      if (p.pgn == pgn && p.name.isNotEmpty) return p.name;
    }
    return '';
  }
}

/// GET /n2k.json through the relay's loopback port. Null when the terminal
/// is not there or its firmware has no bus check.
Future<BusDiag?> readBusDiag({http.Client? client}) async {
  final c = client ?? http.Client();
  try {
    final res = await c
        .get(Uri.parse('http://$kTerminalHttpHost/n2k.json'))
        .timeout(const Duration(seconds: 3));
    if (res.statusCode != 200) return null;
    return BusDiag.parse(res.body);
  } catch (_) {
    return null;
  } finally {
    if (client == null) c.close();
  }
}

/// One answer from GET /n2k/frames.
class FramesChunk {
  const FramesChunk(this.next, this.lost, this.more, this.lines);

  final int next;
  final int lost;
  final bool more;

  /// candump lines, without the newline.
  final List<String> lines;

  /// First line "#next N lost M more 0|1", then one frame a line.
  static FramesChunk? parse(String body) {
    final rows = const LineSplitter().convert(body);
    if (rows.isEmpty) return null;
    final head = rows.first.split(' ');
    if (head.length < 6 || head[0] != '#next') return null;
    final next = int.tryParse(head[1]);
    final lost = int.tryParse(head[3]);
    if (next == null || lost == null) return null;
    return FramesChunk(next, lost, head[5] == '1', [
      for (final r in rows.skip(1))
        if (r.isNotEmpty) r,
    ]);
  }
}

/// Records every CAN frame the terminal decodes into a candump log file,
/// which canboat's analyzer and the firmware host tests can replay.
class BusRecorder extends ChangeNotifier {
  BusRecorder({
    this._client,
    this._folder,
    this.poll = const Duration(milliseconds: 500),
    this.src,
  });

  /// Only this device's frames, or every frame when null.
  final int? src;

  final http.Client? _client;
  final Directory? _folder;
  final Duration poll;

  bool _running = false;
  File? _file;
  IOSink? _sink;
  int _next = 0;
  int frames = 0;
  int lost = 0;
  int failures = 0;
  DateTime? startedAt;
  String? error;

  bool get running => _running;
  File? get file => _file;

  Future<void> start() async {
    if (_running) return;
    final dir = _folder ?? Directory.systemTemp;
    final now = DateTime.now();
    String two(int n) => n.toString().padLeft(2, '0');
    final name =
        'lmyc-bus${src == null ? '' : '-dev$src'}-${now.year}${two(now.month)}${two(now.day)}-'
        '${two(now.hour)}${two(now.minute)}${two(now.second)}.log';
    final f = File('${dir.path}/$name');
    _sink = f.openWrite();
    _sink!.writeln(
      '# LMYC boat terminal bus capture, candump log format. '
      'Times are seconds since the terminal started.',
    );
    _sink!.writeln('# started ${now.toIso8601String()}');
    _file = f;
    frames = 0;
    lost = 0;
    failures = 0;
    error = null;
    _next = 0;
    startedAt = now;
    _running = true;
    notifyListeners();
    unawaited(_loop());
  }

  Future<void> stop() async {
    if (!_running) return;
    _running = false;
    _sink?.writeln(
      '# stopped ${DateTime.now().toIso8601String()}: '
      '$frames frames, $lost lost',
    );
    await _sink?.flush();
    await _sink?.close();
    _sink = null;
    notifyListeners();
  }

  Future<void> _loop() async {
    final c = _client ?? http.Client();
    try {
      while (_running) {
        final more = await _once(c);
        if (!_running) break;
        if (!more) await Future<void>.delayed(poll);
      }
    } finally {
      if (_client == null) c.close();
    }
  }

  /// One poll. True when the terminal had more than fitted in one answer.
  Future<bool> _once(http.Client c) async {
    try {
      final res = await c
          .get(
            Uri.parse(
              'http://$kTerminalHttpHost/n2k/frames?after=$_next'
              '${src == null ? '' : '&src=$src'}',
            ),
          )
          .timeout(const Duration(seconds: 3));
      final chunk = res.statusCode == 200 ? FramesChunk.parse(res.body) : null;
      if (chunk == null) {
        _failed(
          'The terminal did not answer with frames (HTTP ${res.statusCode}).',
        );
        return false;
      }
      if (chunk.lost > 0) {
        lost += chunk.lost;
        _sink?.writeln('# ${chunk.lost} frames lost here');
      }
      for (final l in chunk.lines) {
        _sink?.writeln(l);
      }
      frames += chunk.lines.length;
      _next = chunk.next;
      error = null;
      notifyListeners();
      return chunk.more;
    } catch (e) {
      _failed('Lost the terminal: $e');
      return false;
    }
  }

  void _failed(String why) {
    failures++;
    error = why;
    notifyListeners();
  }

  @override
  void dispose() {
    unawaited(stop());
    super.dispose();
  }
}

class BusCheckPage extends StatefulWidget {
  const BusCheckPage({super.key, this.read, this.recorder});

  /// Where the picture comes from. Defaults to [readBusDiag].
  final Future<BusDiag?> Function()? read;
  final BusRecorder? recorder;

  @override
  State<BusCheckPage> createState() => _BusCheckPageState();
}

class _BusCheckPageState extends State<BusCheckPage> {
  BusDiag? _diag;
  bool _tried = false;
  Timer? _timer;
  late final BusRecorder _rec = widget.recorder ?? BusRecorder();

  @override
  void initState() {
    super.initState();
    _rec.addListener(_changed);
    unawaited(_refresh());
    _timer = Timer.periodic(
      const Duration(seconds: 2),
      (_) => unawaited(_refresh()),
    );
  }

  void _changed() {
    if (mounted) setState(() {});
  }

  Future<void> _refresh() async {
    final d = await (widget.read ?? readBusDiag)();
    if (!mounted) return;
    setState(() {
      _tried = true;
      if (d != null) _diag = d;
    });
  }

  @override
  void dispose() {
    _timer?.cancel();
    _rec.removeListener(_changed);
    if (widget.recorder == null) _rec.dispose();
    super.dispose();
  }

  Future<void> _shareRecording() async {
    final f = _rec.file;
    if (f == null) return;
    await SharePlus.instance.share(
      ShareParams(
        files: [XFile(f.path, mimeType: 'text/plain')],
        subject: 'LMYC bus capture',
      ),
    );
  }

  Future<void> _shareSnapshot() async {
    final d = _diag;
    if (d == null) return;
    final f = File(
      '${Directory.systemTemp.path}/lmyc-n2k-${DateTime.now().millisecondsSinceEpoch}.json',
    );
    await f.writeAsString(d.raw);
    await SharePlus.instance.share(
      ShareParams(
        files: [XFile(f.path, mimeType: 'application/json')],
        subject: 'LMYC bus check',
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final d = _diag;
    return Scaffold(
      appBar: AppBar(title: const Text('Bus check')),
      body: d == null
          ? Center(
              child: Padding(
                padding: const EdgeInsets.all(28),
                child: Text(
                  _tried
                      ? 'No bus check from the terminal. Join the boat Wi-Fi '
                            'on the Boat tab, or update the terminal\'s firmware.'
                      : 'Reading the terminal…',
                  textAlign: TextAlign.center,
                  style: const TextStyle(fontSize: 16, height: 1.4),
                ),
              ),
            )
          : _body(context, d),
    );
  }

  Widget _body(BuildContext context, BusDiag d) {
    final p = Palette.of(context);
    final ok = d.hint == 'ok';
    final tone = ok
        ? Tone.good
        : (d.hint == 'two_sources' ? Tone.neutral : Tone.caution);
    return ListView(
      padding: const EdgeInsets.fromLTRB(20, 12, 20, 32),
      children: [
        _Banner(
          key: const ValueKey('bus-verdict'),
          color: toneColor(tone),
          icon: ok ? Icons.verified_outlined : Icons.report_problem_outlined,
          title: ok ? _okTitle(d) : 'Needs a look',
          detail: ok ? _okDetail(d) : d.hintText,
        ),
        const SizedBox(height: 12),
        Text(
          d.source == 'sim' ? 'Simulator. Not the boat\'s data.' : _selfLine(d),
          style: TextStyle(color: p.muted, fontSize: 13.5, height: 1.35),
        ),
        const SizedBox(height: 10),
        OutlinedButton.icon(
          key: const ValueKey('bus-open-network'),
          onPressed: () => Navigator.of(
            context,
          ).push(MaterialPageRoute<void>(builder: (_) => const NetworkPage())),
          icon: const Icon(Icons.hub_outlined),
          label: const Text('Network map'),
        ),
        const SizedBox(height: 20),
        SectionLabel('Recording'),
        const SizedBox(height: 8),
        _recording(context),
        const SizedBox(height: 22),
        SectionLabel('Devices'),
        const SizedBox(height: 6),
        if (d.devices.isEmpty) _faint(context, 'None heard yet.'),
        for (final dev in d.devices)
          _Row(
            title: '${dev.src} · ${dev.title}',
            detail:
                '${dev.pgns} PGNs · ${_count(dev.frames)} frames · last ${_age(dev.ageMs)}',
          ),
        const SizedBox(height: 22),
        SectionLabel('Values and the device they come from'),
        const SizedBox(height: 6),
        if (d.owners.isEmpty) _faint(context, 'No instrument values yet.'),
        for (final o in d.owners)
          _Row(
            title: '${o.label} · from ${o.src}',
            detail: o.other == null
                ? 'last ${_age(o.ageMs)}'
                : 'last ${_age(o.ageMs)} · ${o.other} also sends it',
            warn: o.other != null,
          ),
        const SizedBox(height: 22),
        SectionLabel('Converter'),
        const SizedBox(height: 6),
        for (final c in d.conv)
          _Row(
            title: '${c.pgn} ${d.pgnName(c.pgn)}',
            detail:
                'used ${_count(c.used)} · rate-limited ${_count(c.throttled)} · '
                'n/a ${_count(c.notAvailable)} · bad ${_count(c.bad)} · '
                'other device ${_count(c.otherSource)}',
            warn: c.bad > 0,
          ),
        const SizedBox(height: 22),
        SectionLabel('PGNs heard'),
        const SizedBox(height: 6),
        for (final g in d.pgns)
          _Row(
            title: '${g.pgn} ${g.name} · from ${g.src}',
            detail:
                '${g.rate.toStringAsFixed(1)}/s · ${_count(g.messages)} msgs'
                '${g.fast ? ' · fast packet ${_count(g.messages)}/${_count(g.starts)} whole' : ''}'
                ' · last ${_age(g.ageMs)}',
            warn: g.incomplete > 0 && g.incomplete * 10 >= g.starts,
          ),
        const SizedBox(height: 22),
        Text(
          '${_count(d.frames)} frames · ${d.framesPerSecond}/s · lost ${_count(d.lost)} · '
          'bus errors ${_count(d.busErrors)} · repeats ${_count(d.repeats)} · '
          'fast packets incomplete ${_count(d.fastIncomplete)} · bad values ${_count(d.badValues)}',
          style: TextStyle(color: p.faint, fontSize: 12.5),
        ),
        const SizedBox(height: 12),
        OutlinedButton.icon(
          key: const ValueKey('share-bus-check'),
          onPressed: _shareSnapshot,
          icon: const Icon(Icons.ios_share),
          label: const Text('Share this bus check'),
        ),
      ],
    );
  }

  Widget _recording(BuildContext context) {
    final p = Palette.of(context);
    final r = _rec;
    final status = r.running
        ? 'Recording · ${_count(r.frames)} frames'
              '${r.lost > 0 ? ' · ${_count(r.lost)} lost' : ''}'
        : r.file == null
        ? 'Saves every frame on the bus to a file you can share.'
        : 'Saved ${_count(r.frames)} frames'
              '${r.lost > 0 ? ' · ${_count(r.lost)} lost' : ''}';
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(status, style: TextStyle(color: p.body, fontSize: 14.5)),
        if (r.error != null)
          Padding(
            padding: const EdgeInsets.only(top: 4),
            child: Text(
              r.error!,
              style: TextStyle(color: toneColor(Tone.caution), fontSize: 13),
            ),
          ),
        const SizedBox(height: 10),
        Wrap(
          spacing: 10,
          runSpacing: 10,
          children: [
            FilledButton.icon(
              key: const ValueKey('record-bus'),
              onPressed: () => unawaited(r.running ? r.stop() : r.start()),
              icon: Icon(r.running ? Icons.stop : Icons.fiber_manual_record),
              label: Text(r.running ? 'Stop' : 'Record bus'),
            ),
            if (!r.running && r.file != null)
              OutlinedButton.icon(
                key: const ValueKey('share-recording'),
                onPressed: _shareRecording,
                icon: const Icon(Icons.ios_share),
                label: const Text('Share recording'),
              ),
          ],
        ),
      ],
    );
  }

  Widget _faint(BuildContext context, String text) => Text(
    text,
    style: TextStyle(color: Palette.of(context).faint, fontSize: 13.5),
  );

  /// This terminal on the bus, in words a member can check against the
  /// plotter's device list.
  String _selfLine(BusDiag d) => switch (d.node) {
    'claimed' =>
      'This terminal is NMEA 2000 address ${d.address}. The plotter\'s device '
          'list shows it as LMYC boat-terminal. It sends no instrument data.',
    'claiming' => 'This terminal is claiming an NMEA 2000 address.',
    'no_address' => 'This terminal could not claim an NMEA 2000 address: every address is taken.',
    _ => 'This terminal is not on a bus.',
  };

  String _okTitle(BusDiag d) => switch (d.state) {
    'live' => 'Bus looks healthy',
    'sim' || 'paused' => 'Simulator',
    _ => 'Waiting for the bus',
  };

  String _okDetail(BusDiag d) =>
      '${d.devices.length} ${d.devices.length == 1 ? 'device' : 'devices'}, '
      '${d.framesPerSecond} frames a second, nothing lost or rejected.';
}

String _count(int n) => formatCount(n);

String _age(int ms) {
  if (ms >= 0xFFFFFFFF || ms < 0) return 'never';
  if (ms < 1000) return 'now';
  if (ms < 60000) return '${ms ~/ 1000} s ago';
  return '${ms ~/ 60000} min ago';
}

class _Banner extends StatelessWidget {
  const _Banner({
    super.key,
    required this.color,
    required this.icon,
    required this.title,
    required this.detail,
  });

  final Color color;
  final IconData icon;
  final String title;
  final String detail;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(14),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, color: color),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  style: TextStyle(
                    color: color,
                    fontWeight: FontWeight.w700,
                    fontSize: 16,
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  detail,
                  style: TextStyle(
                    color: Palette.of(context).body,
                    height: 1.35,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _Row extends StatelessWidget {
  const _Row({required this.title, required this.detail, this.warn = false});

  final String title;
  final String detail;
  final bool warn;

  @override
  Widget build(BuildContext context) {
    final p = Palette.of(context);
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            title,
            style: TextStyle(
              color: warn ? toneColor(Tone.caution) : p.ink,
              fontWeight: FontWeight.w600,
              fontSize: 14.5,
            ),
          ),
          const SizedBox(height: 2),
          Text(
            detail,
            style: TextStyle(
              color: p.muted,
              fontSize: 13,
              fontFeatures: const [FontFeature.tabularFigures()],
            ),
          ),
        ],
      ),
    );
  }
}
