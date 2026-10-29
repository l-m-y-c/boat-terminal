import 'dart:convert';
import 'dart:io';

import 'package:archive/archive.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:lmyc_app/diag_bundle.dart';
import 'package:lmyc_app/diag_bundle_card.dart';
import 'package:lmyc_app/terminal_stats.dart';

TerminalStats stats({
  String src = 'n2k',
  String state = 'live',
  int fps = 50,
  int busErr = 0,
}) => TerminalStats.parse(
  jsonEncode({
    'v': 1,
    'src': src,
    'state': state,
    'fps': fps,
    'bus_err': busErr,
  }),
)!;

void main() {
  final frames = [
    for (var i = 1; i <= 7; i++)
      '($i.000000) can0 09F80101#${i.toRadixString(16).padLeft(2, '0')}00000000000000',
  ];

  MockClient terminal() => MockClient((req) async {
    final after = int.tryParse(req.url.queryParameters['after'] ?? '0') ?? 0;
    switch (req.url.path) {
      case '/status.json':
        return http.Response('{"boat":"Baca Lele","tcp":1}', 200);
      case '/n2k.json':
        return http.Response('{"state":"live"}', 200);
      case '/n2k/network.json':
        return http.Response(
          '{"self":{"model":"LMYC boat-terminal","software":"0.9 a1358e7"},"devices":[{"src":1,"model":"GPSMAP 740s"}]}',
          200,
        );
      case '/n2k/frames':
        final start = after < 1 ? 1 : after;
        final chunk = frames.skip(start - 1).take(3).toList();
        final next = start + chunk.length;
        final more = next <= frames.length ? 1 : 0;
        return http.Response(
          '#next $next lost 0 more $more\n${chunk.join('\n')}',
          200,
        );
      case '/diag/nmea.txt':
        return http.Response(
          '#next 2 lost 0 more 0\n1.000 S \$GPRMC,x*00\n1.100 L tcp client\n',
          200,
        );
    }
    return http.Response('', 404);
  });

  test('ring pages parse', () {
    final p = RingPage.parse('#next 4 lost 2 more 1\na\nb\n')!;
    expect(
      [p.next, p.lost, p.more, p.rows],
      [
        4,
        2,
        true,
        ['a', 'b'],
      ],
    );
    expect(RingPage.parse('<html>'), isNull);
  });

  test('file name is timestamped and safe', () {
    expect(
      diagFileName(DateTime(2026, 10, 29, 10, 5, 7), 'auto-bus off'),
      'lmyc-diag-20261029-100507-auto-bus-off.zip',
    );
  });

  test('collect writes one zip with the terminal and phone parts', () async {
    final dir = await Directory.systemTemp.createTemp('diag');
    final steps = <String>[];
    final r = await DiagCollector(client: terminal()).collect(
      dir: dir,
      reason: 'manual',
      note: 'no COG in Navionics',
      phone: {'app': '1.0+1'},
      phoneFiles: {'phone/relay-log.txt': 'x connected\n'},
      onProgress: (s, _) => steps.add(s),
    );
    expect(r.terminalReached, isTrue);
    expect(r.frames, 7);
    expect(r.ringEntries, 2);
    expect(steps.last, 'Saved');
    final zip = ZipDecoder().decodeBytes(await r.file.readAsBytes());
    String read(String n) => utf8.decode(zip.findFile(n)!.content as List<int>);
    expect(
      read('terminal/bus-ring.log')
          .split('\n')
          .where((l) => l.startsWith('('))
          .toList(),
      frames,
    );
    expect(read('terminal/network.json'), contains('GPSMAP 740s'));
    expect(read('terminal/nmea-ring.txt'), contains('GPRMC'));
    expect(read('phone/relay-log.txt'), contains('connected'));
    final m = jsonDecode(read('manifest.json')) as Map;
    expect(m['note'], 'no COG in Navionics');
    expect(m['terminal_reached'], isTrue);
    await dir.delete(recursive: true);
  });

  test('an unreachable terminal still saves the phone side', () async {
    final dir = await Directory.systemTemp.createTemp('diag');
    final r = await DiagCollector(
      client: MockClient((_) async => throw const SocketException('no route')),
    ).collect(dir: dir, reason: 'manual', phoneFiles: {'phone/a.txt': 'a'});
    expect(r.terminalReached, isFalse);
    expect(r.file.existsSync(), isTrue);
    await dir.delete(recursive: true);
  });

  test('old bundles are pruned', () async {
    final dir = await Directory.systemTemp.createTemp('diag');
    for (var i = 0; i < 5; i++) {
      File('${dir.path}/lmyc-diag-2026010$i-000000-x.zip')
          .writeAsStringSync('');
    }
    await pruneBundles(dir, keep: 2);
    final left = await listBundles(dir);
    expect(left.map((f) => f.path.split('/').last), [
      'lmyc-diag-20260104-000000-x.zip',
      'lmyc-diag-20260103-000000-x.zip',
    ]);
    await dir.delete(recursive: true);
  });

  group('automatic snapshot', () {
    final t0 = DateTime(2026, 10, 29, 12);
    test('bus-off fires once per cooldown', () {
      final a = AutoSnapshot();
      expect(a.observe(stats(state: 'bus_off'), t0), 'bus-off');
      expect(
        a.observe(stats(state: 'bus_off'), t0.add(const Duration(minutes: 1))),
        isNull,
      );
      expect(
        a.observe(stats(state: 'bus_off'), t0.add(const Duration(minutes: 16))),
        'bus-off',
      );
    });
    test('a jump in bus errors fires', () {
      final a = AutoSnapshot();
      expect(a.observe(stats(busErr: 10), t0), isNull);
      expect(a.observe(stats(busErr: 100), t0), isNull);
      expect(a.observe(stats(busErr: 300), t0), 'bus-errors');
    });
    test('frames stopping after having flowed fires', () {
      final a = AutoSnapshot();
      expect(a.observe(stats(fps: 0), t0), isNull, reason: 'never had frames');
      expect(a.observe(stats(fps: 40), t0), isNull);
      expect(
        a.observe(
          stats(fps: 0, state: 'quiet'),
          t0.add(const Duration(seconds: 2)),
        ),
        isNull,
      );
      expect(
        a.observe(
          stats(fps: 0, state: 'quiet'),
          t0.add(const Duration(seconds: 25)),
        ),
        'no-frames',
      );
    });
    test('the simulator never fires', () {
      final a = AutoSnapshot();
      expect(a.observe(stats(src: 'sim', state: 'bus_off'), t0), isNull);
    });
  });

  testWidgets('the card collects and says when the terminal is away', (
    tester,
  ) async {
    final dir = Directory.systemTemp.createTempSync('diag');
    final c = DiagBundleController(
      dir: () async => dir,
      phoneContext: () async => (<String, Object?>{}, <String, String>{}),
      collector: DiagCollector(
        client: MockClient((_) async => throw const SocketException('x')),
      ),
    );
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(child: DiagBundleCard(controller: c)),
        ),
      ),
    );
    await tester.runAsync(() => c.collect('manual'));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('diag-bundle-error')), findsOneWidget);
    expect(find.textContaining('lmyc-diag-'), findsOneWidget);
    dir.deleteSync(recursive: true);
  });
}
