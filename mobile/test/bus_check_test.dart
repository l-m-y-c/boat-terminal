import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:lmyc_app/bus_check.dart';
import 'package:lmyc_app/instruments.dart';
import 'package:lmyc_app/terminal_stats.dart';

/// GET /n2k.json captured from the terminal on the simulator.
final _fixture = File('test/fixtures/n2k_sim.json').readAsStringSync();

void main() {
  group('BusDiag', () {
    test('parses the terminal\'s own /n2k.json', () {
      final d = BusDiag.parse(_fixture)!;
      expect(d.source, 'sim');
      expect(d.hint, 'ok');
      expect(d.devices, hasLength(6));
      expect(d.pgns, hasLength(15));
      expect(d.owners.map((o) => o.what), contains('depth'));
      final depth = d.conv.firstWhere((c) => c.pgn == 128267);
      expect(
        depth.used +
            depth.throttled +
            depth.bad +
            depth.notAvailable +
            depth.otherSource,
        depth.received,
      );
      expect(d.pgnName(129029), 'GNSS position');
    });

    test('names a Garmin from its address claim and product info', () {
      final d = BusDiag.parse(
        '{"v":1,"bus":{"src":"n2k","state":"live","hint":"repeats",'
        '"hint_text":"x"},"self":{"node":"claimed","address":23,"tx_frames":9},"devices":[{"src":35,"frames":10,"pgns":3,"age_ms":5,'
        '"mfr":229,"maker":"Garmin","class_name":"Display","model":"GPSMAP 740s"},'
        '{"src":7,"frames":1,"pgns":1,"age_ms":5,"mfr":1234,"maker":""}],'
        '"pgns":[{"pgn":129029,"name":"GNSS position","src":35,"fast":true,'
        '"frames":20,"starts":10,"msgs":7,"rate":1.0,"age_ms":5}],"conv":[],'
        '"sources":[{"what":"depth","src":35,"age_ms":3,"other":7}]}',
      )!;
      expect(d.node, 'claimed');
      expect(d.address, 23);
      expect(d.txFrames, 9);
      expect(d.devices[0].title, 'Garmin GPSMAP 740s');
      expect(d.devices[1].title, 'Maker 1234');
      expect(d.pgns.single.incomplete, 3);
      expect(d.owners.single.other, 7);
      expect(d.owners.single.label, 'Depth');
    });

    test('rejects anything else', () {
      expect(BusDiag.parse('{"v":1,"warming":true}'), isNull);
      expect(BusDiag.parse('nope'), isNull);
    });
  });

  test('FramesChunk reads the header and the candump lines', () {
    final c = FramesChunk.parse(
      '#next 42 lost 3 more 1\n'
      '(12.000100) can0 09F80123#0102030405060708\n'
      '(12.000900) can0 0DF80523#AA\n',
    )!;
    expect(c.next, 42);
    expect(c.lost, 3);
    expect(c.more, isTrue);
    expect(c.lines, hasLength(2));
    expect(FramesChunk.parse('garbage'), isNull);
  });

  test('BusRecorder writes every frame and notes losses', () async {
    final dir = await Directory.systemTemp.createTemp('lmyc-rec');
    final seen = <String>[];
    var calls = 0;
    final client = MockClient((req) async {
      seen.add(req.url.query);
      calls++;
      return switch (calls) {
        1 => http.Response('#next 10 lost 0 more 0\n', 200),
        2 => http.Response(
          '#next 12 lost 2 more 1\n(1.0) can0 09F80123#01\n(1.1) can0 09F80123#02\n',
          200,
        ),
        _ => http.Response('#next 12 lost 0 more 0\n', 200),
      };
    });
    final rec = BusRecorder(
      client: client,
      folder: dir,
      poll: const Duration(milliseconds: 5),
    );
    await rec.start();
    await Future<void>.delayed(const Duration(milliseconds: 80));
    await rec.stop();
    expect(seen.first, 'after=0');
    expect(seen, contains('after=10'));
    expect(seen, contains('after=12'));
    expect(rec.frames, 2);
    expect(rec.lost, 2);
    final text = await rec.file!.readAsString();
    expect(text, contains('# 2 frames lost here'));
    expect(text, contains('(1.0) can0 09F80123#01'));
    expect(text, contains('# stopped'));
    await dir.delete(recursive: true);
  });

  group('depth reference', () {
    test('DPT offset: keel, surface, transducer', () {
      final r = InstrumentReadings();
      r.note(r'$SDDPT,2.0,-1.8,*5D');
      expect(r.depthM, closeTo(0.2, 1e-9));
      expect(r.depthRef, DepthRef.keel);
      r.note(r'$SDDPT,2.0,0.4,');
      expect(r.depthM, closeTo(2.4, 1e-9));
      expect(r.depthRef, DepthRef.surface);
      r.note(r'$SDDPT,2.0,0.0,');
      expect(r.depthRef, DepthRef.transducer);
    });

    test('no-fix RMC and VTG leave speed alone', () {
      final r = InstrumentReadings();
      r.note(
        r'$GPRMC,123500.00,V,3351.4080,S,15112.9180,E,4.0,10.0,171026,,,N',
      );
      r.note(r'$GPVTG,10.0,T,,M,4.0,N,7.4,K,N');
      expect(r.sogKn, isNull);
      r.note(r'$GPRMC,123500.00,A,3351.4080,S,15112.9180,E,0.0,,171026,,,A');
      expect(r.sogKn, 0.0);
      expect(r.cogDeg, isNull);
    });
  });

  test('stats carry the address and the hint, worded like the terminal', () {
    final s = TerminalStats.parse(
      '{"v":1,"src":"n2k","state":"live","addr":23,"hint":"repeats"}',
    )!;
    expect(s.address, 23);
    expect(s.hintText, contains('not getting acknowledgements'));
    expect(
      TerminalStats.parse('{"v":1,"src":"n2k","state":"live","addr":-1}')!
          .address,
      isNull,
    );
    expect(
      TerminalStats.parse('{"v":1,"src":"n2k","state":"live"}')!.hintText,
      isNull,
    );
  });

  testWidgets('Bus check shows the verdict, devices and owners', (
    tester,
  ) async {
    final d = BusDiag.parse(_fixture)!;
    await tester.pumpWidget(
      MaterialApp(home: BusCheckPage(read: () async => d)),
    );
    await tester.pump();
    expect(find.byKey(const ValueKey('bus-verdict')), findsOneWidget);
    expect(find.text('Simulator'), findsOneWidget);
    expect(find.byKey(const ValueKey('record-bus')), findsOneWidget);
    await tester.pumpWidget(const SizedBox());
  });
}
