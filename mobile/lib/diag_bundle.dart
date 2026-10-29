// Collect diagnostics: the terminal's diag bundle over the boat Wi-Fi plus the
// phone's own side, saved as one zip the bench can pull or the member shares.
//
// With the enclosure closed the USB-C ports are out of reach, so this is how
// debug data leaves the terminal. Its rings are RAM-only, so the app also
// snapshots automatically the moment the bus goes wrong (AutoSnapshot).
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:archive/archive.dart';
import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;

import 'boat_wifi.dart';
import 'terminal_stats.dart';

/// Where bundles go, under the app's external files dir:
/// /sdcard/Android/data/ca.lmyc.boat/files/diagnostics/ (adb-pullable,
/// no permission needed). tools/boat_diag.py pulls and unpacks them.
const kDiagFolder = 'diagnostics';

/// Bundles kept on the phone; the oldest go first.
const kDiagKeep = 30;

typedef DiagProgress = void Function(String step, double fraction);

class DiagBundleResult {
  const DiagBundleResult({
    required this.file,
    required this.reason,
    required this.terminalReached,
    required this.frames,
    required this.ringEntries,
    required this.missing,
  });

  final File file;
  final String reason;
  final bool terminalReached;
  final int frames;
  final int ringEntries;
  final List<String> missing;

  String get summary => terminalReached
      ? '$frames bus frames, $ringEntries log entries'
            '${missing.isEmpty ? '' : ', missing: ${missing.join(', ')}'}'
      : 'terminal not reachable: phone side only';
}

/// One paged ring ("#next N lost M more 0|1" then lines), read to the end.
class RingPage {
  RingPage(this.next, this.lost, this.more, this.rows);
  final int next;
  final int lost;
  final bool more;
  final List<String> rows;

  static RingPage? parse(String body) {
    final lines = const LineSplitter().convert(body);
    if (lines.isEmpty) return null;
    final h = lines.first.trim().split(RegExp(r'\s+'));
    if (h.length < 6 || h[0] != '#next') return null;
    final next = int.tryParse(h[1]);
    final lost = int.tryParse(h[3]);
    if (next == null || lost == null) return null;
    return RingPage(
      next,
      lost,
      h[5] == '1',
      lines.skip(1).where((l) => l.isNotEmpty).toList(),
    );
  }
}

/// File name for a bundle: lmyc-diag-20261029-105600-manual.zip (local time).
String diagFileName(DateTime t, String reason) {
  String two(int v) => v.toString().padLeft(2, '0');
  final safe = reason.toLowerCase().replaceAll(RegExp(r'[^a-z0-9]+'), '-');
  return 'lmyc-diag-${t.year}${two(t.month)}${two(t.day)}-'
      '${two(t.hour)}${two(t.minute)}${two(t.second)}-$safe.zip';
}

class DiagCollector {
  DiagCollector({
    http.Client? client,
    this.host = kTerminalHttpHost,
    this.pageTimeout = const Duration(seconds: 8),
  }) : _client = client; // ignore: prefer_initializing_formals

  final http.Client? _client;
  final String host;
  final Duration pageTimeout;

  Future<String?> _get(http.Client c, String path) async {
    try {
      final r = await c
          .get(Uri.parse('http://$host$path'))
          .timeout(pageTimeout);
      return r.statusCode == 200 ? r.body : null;
    } catch (_) {
      return null;
    }
  }

  Future<(List<String>, int)?> _drain(
    http.Client c,
    String path,
    int after,
    void Function(int rows)? onRows,
  ) async {
    final rows = <String>[];
    var lost = 0;
    for (var i = 0; i < 4000; i++) {
      final body = await _get(c, '$path?after=$after');
      final page = body == null ? null : RingPage.parse(body);
      if (page == null) return rows.isEmpty ? null : (rows, lost);
      rows.addAll(page.rows);
      lost += page.lost;
      onRows?.call(rows.length);
      if (!page.more || page.next <= after) break;
      after = page.next;
    }
    return (rows, lost);
  }

  /// Pulls everything and writes the zip into [dir]. Never throws for a
  /// missing terminal: the phone side is still worth keeping.
  Future<DiagBundleResult> collect({
    required Directory dir,
    required String reason,
    String? note,
    DiagProgress? onProgress,
    Map<String, Object?> phone = const {},
    Map<String, String> phoneFiles = const {},
    DateTime? now,
  }) async {
    final at = now ?? DateTime.now();
    final c = _client ?? http.Client();
    final files = <String, String>{};
    final missing = <String>[];
    var frames = 0;
    var ring = 0;
    var reached = false;
    void step(String s, double f) => onProgress?.call(s, f);
    try {
      step('Reaching the terminal', 0.02);
      final status = await _get(c, '/status.json');
      reached = status != null;
      if (reached) {
        files['terminal/status.json'] = status;
        step('Bus picture', 0.08);
        for (final (name, path) in [
          ('n2k.json', '/n2k.json'),
          ('network.json', '/n2k/network.json'),
        ]) {
          final b = await _get(c, path);
          if (b == null) {
            missing.add(name);
          } else {
            files['terminal/$name'] = b;
          }
        }
        step('Bus frames', 0.15);
        final f = await _drain(
          c,
          '/n2k/frames',
          1,
          (n) => step('Bus frames: $n', 0.15 + 0.6 * (n / 32768).clamp(0, 1)),
        );
        if (f == null) {
          missing.add('bus frames');
        } else {
          frames = f.$1.length;
          files['terminal/bus-ring.log'] =
              '# LMYC boat terminal capture ring (incl. its own TX), candump log format. '
              'Times are seconds since the terminal started.\n'
              '# fetched ${at.toIso8601String()} by the phone app, '
              '${f.$2} older frames overwritten\n'
              '${f.$1.join('\n')}\n';
        }
        step('Sentence and event log', 0.8);
        final s = await _drain(c, '/diag/nmea.txt', 0, null);
        if (s == null) {
          missing.add('sentence log');
        } else {
          ring = s.$1.length;
          files['terminal/nmea-ring.txt'] = '${s.$1.join('\n')}\n';
        }
      }
    } finally {
      if (_client == null) c.close();
    }
    step('Phone side', 0.9);
    files.addAll(phoneFiles);
    final manifest = <String, Object?>{
      'kind': 'lmyc-diag',
      'version': 1,
      'collected': at.toIso8601String(),
      'collected_utc': at.toUtc().toIso8601String(),
      'reason': reason,
      if (note != null && note.isNotEmpty) 'note': note,
      'terminal_reached': reached,
      'terminal_host': host,
      'frames': frames,
      'ring_entries': ring,
      'missing': missing,
      'phone': phone,
    };
    files['manifest.json'] = const JsonEncoder.withIndent('  ')
        .convert(manifest);
    step('Saving', 0.95);
    final archive = Archive();
    final names = files.keys.toList()..sort();
    for (final n in names) {
      archive.addFile(ArchiveFile.string(n, files[n]!));
    }
    final bytes = ZipEncoder().encode(archive);
    await dir.create(recursive: true);
    final out = File('${dir.path}/${diagFileName(at, reason)}');
    await out.writeAsBytes(bytes, flush: true);
    await pruneBundles(dir);
    step('Saved', 1);
    return DiagBundleResult(
      file: out,
      reason: reason,
      terminalReached: reached,
      frames: frames,
      ringEntries: ring,
      missing: missing,
    );
  }
}

/// Newest first.
Future<List<File>> listBundles(Directory dir) async {
  if (!dir.existsSync()) return [];
  final all = await dir
      .list()
      .where(
        (e) =>
            e is File &&
            e.path.endsWith('.zip') &&
            e.path.contains('lmyc-diag-'),
      )
      .cast<File>()
      .toList();
  all.sort((a, b) => b.path.compareTo(a.path));
  return all;
}

Future<void> pruneBundles(Directory dir, {int keep = kDiagKeep}) async {
  final all = await listBundles(dir);
  for (final f in all.skip(keep)) {
    try {
      await f.delete();
    } catch (_) {}
  }
}

/// Decides when to snapshot by itself, from the 2 s Bluetooth stats: the
/// terminal's rings are RAM-only, so grab them while the problem is there.
class AutoSnapshot {
  AutoSnapshot({
    this.quietAfter = const Duration(seconds: 20),
    this.cooldown = const Duration(minutes: 15),
    this.errorJump = 200,
  });

  final Duration quietAfter;
  final Duration cooldown;
  final int errorJump;

  final Map<String, DateTime> _last = {};
  int? _errorsBase;
  DateTime? _quietSince;
  bool _hadFrames = false;

  /// A reason to snapshot now, or null. Only on NMEA 2000.
  String? observe(TerminalStats s, DateTime now) {
    if (s.mode != DataMode.nmea2000) {
      _errorsBase = null;
      _quietSince = null;
      _hadFrames = false;
      return null;
    }
    String? reason;
    if (s.state == BusState.busOff || s.state == BusState.failed) {
      reason = 'bus-off';
    }
    final base = _errorsBase;
    if (base == null || s.busErrors < base) {
      _errorsBase = s.busErrors;
    } else if (s.busErrors - base >= errorJump) {
      reason ??= 'bus-errors';
      _errorsBase = s.busErrors;
    }
    if (s.framesPerSecond > 0) {
      _hadFrames = true;
      _quietSince = null;
    } else if (_hadFrames) {
      _quietSince ??= now;
      if (now.difference(_quietSince!) >= quietAfter) reason ??= 'no-frames';
    }
    if (reason == null) return null;
    final prev = _last[reason];
    if (prev != null && now.difference(prev) < cooldown) return null;
    _last[reason] = now;
    return reason;
  }
}

/// Owned by the home page so automatic snapshots happen on any tab.
class DiagBundleController extends ChangeNotifier {
  DiagBundleController({
    required this.dir,
    required this.phoneContext,
    DiagCollector? collector,
    AutoSnapshot? auto,
  }) : collector = collector ?? DiagCollector(),
       auto = auto ?? AutoSnapshot();

  /// Resolves the bundle folder (the platform call can fail: null then).
  final Future<Directory?> Function() dir;

  /// Phone facts and files for the bundle, gathered at collect time.
  final Future<(Map<String, Object?>, Map<String, String>)> Function()
  phoneContext;
  final DiagCollector collector;
  final AutoSnapshot auto;

  bool busy = false;
  String step = '';
  double progress = 0;
  DiagBundleResult? last;
  String? error;
  List<File> bundles = [];

  Future<void> refresh() async {
    final d = await dir();
    if (d == null) return;
    bundles = await listBundles(d);
    notifyListeners();
  }

  Future<DiagBundleResult?> collect(String reason, {String? note}) async {
    if (busy) return null;
    busy = true;
    error = null;
    step = 'Starting';
    progress = 0;
    notifyListeners();
    try {
      final d = await dir();
      if (d == null) {
        throw StateError('No storage for diagnostics on this phone.');
      }
      final (phone, files) = await phoneContext();
      final r = await collector.collect(
        dir: d,
        reason: reason,
        note: note,
        phone: phone,
        phoneFiles: files,
        onProgress: (s, f) {
          step = s;
          progress = f;
          notifyListeners();
        },
      );
      last = r;
      if (!r.terminalReached) {
        error =
            'The terminal did not answer over the boat Wi-Fi. '
            'Saved the phone side only.';
      }
      bundles = await listBundles(d);
      return r;
    } catch (e) {
      error = 'Could not save diagnostics: $e';
      return null;
    } finally {
      busy = false;
      notifyListeners();
    }
  }

  /// Feed every stats read; collects in the background when it should.
  void observe(TerminalStats s, {DateTime? now}) {
    final reason = auto.observe(s, now ?? DateTime.now());
    if (reason != null && !busy) unawaited(collect('auto-$reason'));
  }
}
