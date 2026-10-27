import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:lmyc_app/bus_check.dart';
import 'package:lmyc_app/network_page.dart';

/// Captured from the terminal on the simulator with `NMEA NET` and `NMEA NET 12`.
final _network = File('test/fixtures/n2k_network_sim.json')
    .readAsStringSync()
    .trim();
final _device = File('test/fixtures/n2k_device_12_sim.json')
    .readAsStringSync()
    .trim();

void main() {
  test(
    'the simulated network parses: terminal plus six devices, all complete',
    () {
      final n = Network.parse(_network)!;
      expect(n.devices, hasLength(7));
      expect(n.lenTotal, 19);
      expect(n.asking, 0);
      expect(n.problems, 0);
      final ordered = n.ordered;
      expect(ordered.first.self, isTrue);
      expect(ordered.first.title, 'LMYC Boat Terminal');
      final depth = n.devices.firstWhere((d) => d.address == 12);
      expect(depth.title, 'Sim depth / speed / temp');
      expect(depth.role, 'Depth / speed / temp');
      expect(depth.install1, 'Sim: forward of the keel');
      expect(depth.len, 1);
      expect(depth.transmits, containsAll([128267, 128259, 130316]));
      expect(depth.complete, isTrue);
      // Class order: navigation (60) before environment (85).
      final classes = ordered.skip(1).map((d) => d.deviceClass).toList();
      expect(classes, orderedEquals([...classes]..sort()));
    },
  );

  test('a device page parses: what it owns, PGNs decoded, recent messages', () {
    final d = DeviceDetail.parse(_device)!;
    expect(d.device.address, 12);
    expect(d.owns, containsAll(['depth', 'water_speed', 'sea_temp']));
    final depth = d.pgns.firstWhere((p) => p.pgn == 128267);
    expect(depth.text, startsWith('Depth '));
    expect(depth.hex, isNotEmpty);
    expect(d.recent, isNotEmpty);
  });

  test('issues come through with their text', () {
    final n = Network.parse(
      '{"v":1,"len_total":2,"devices":[{"address":50,"self":false,"title":"Address 50",'
      '"class":60,"function":145,"complete":false,"issues":[{"code":"instance_conflict",'
      '"text":"Another device has the same class, function and instance."},'
      '{"code":"silent","text":"Nothing from it for 10 s."}]}]}',
    )!;
    expect(n.problems, 1);
    expect(n.devices.single.silent, isTrue);
    expect(n.devices.single.issues.first.code, 'instance_conflict');
  });

  test('Record this device asks for that device only', () async {
    final dir = await Directory.systemTemp.createTemp('lmyc-dev-rec');
    final asked = <String>[];
    final rec = BusRecorder(
      client: MockClient((req) async {
        asked.add(req.url.query);
        return http.Response(
          '#next 3 lost 0 more 0\n(1.0) can0 0DF50B0C#01\n',
          200,
        );
      }),
      folder: dir,
      poll: const Duration(milliseconds: 5),
      src: 12,
    );
    await rec.start();
    await Future<void>.delayed(const Duration(milliseconds: 40));
    await rec.stop();
    expect(asked.first, 'after=0&src=12');
    expect(rec.file!.path, contains('lmyc-bus-dev12-'));
    await dir.delete(recursive: true);
  });

  testWidgets('map shows every node; tapping one opens its details', (
    tester,
  ) async {
    final n = Network.parse(_network)!;
    final d = DeviceDetail.parse(_device)!;
    await tester.pumpWidget(
      MaterialApp(
        home: NetworkPage(
          read: ({bool refresh = false}) async => n,
          readDetail: (address) async => d,
        ),
      ),
    );
    await tester.pump();
    for (final dev in n.devices) {
      expect(find.byKey(ValueKey('net-node-${dev.address}')), findsOneWidget);
    }
    await tester.ensureVisible(find.byKey(const ValueKey('net-node-12')));
    await tester.tap(find.byKey(const ValueKey('net-node-12')));
    await tester.pumpAndSettle(const Duration(milliseconds: 300));
    expect(find.text('Sim depth / speed / temp'), findsWidgets);
    expect(find.textContaining('Depth '), findsWidgets);
    final page = find
        .descendant(
          of: find.byType(DeviceDetailPage),
          matching: find.byType(Scrollable),
        )
        .first;
    await tester.scrollUntilVisible(
      find.byKey(const ValueKey('toggle-hex')),
      200,
      scrollable: page,
    );
    await tester.tap(find.byKey(const ValueKey('toggle-hex')));
    await tester.pump();
    expect(find.text('Decoded'), findsOneWidget);
    await tester.scrollUntilVisible(
      find.byKey(const ValueKey('record-device')),
      300,
      scrollable: page,
    );
    expect(find.byKey(const ValueKey('record-device')), findsOneWidget);
    await tester.pumpWidget(const SizedBox());
  });
}
