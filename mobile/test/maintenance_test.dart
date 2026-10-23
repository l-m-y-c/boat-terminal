import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:image/image.dart' as img;
import 'package:lmyc_app/main.dart';
import 'package:lmyc_app/maintenance_api.dart';
import 'package:lmyc_app/maintenance_book.dart';
import 'package:lmyc_app/maintenance_jpeg.dart';
import 'package:lmyc_app/protocol.dart';

void main() {
  test('long edge becomes 2000 and a small photo is not enlarged', () {
    final wide = img.encodeJpg(img.Image(width: 2400, height: 1200));
    final fitted = fitMaintenanceJpeg(wide);
    final decoded = img.decodeJpg(fitted)!;
    expect(decoded.width, 2000);
    expect(decoded.height, 1000);
    expect(fitted.length, lessThanOrEqualTo(kMaintPhotoBytesMax));
    expect(fitted[0], 0xFF);
    expect(fitted[1], 0xD8);

    final small = img.encodeJpg(img.Image(width: 100, height: 80));
    final same = fitMaintenanceJpeg(small);
    final decodedSmall = img.decodeJpg(same)!;
    expect(decodedSmall.width, 100);
    expect(decodedSmall.height, 80);
  });

  test(
    'a photo note is sent with the JPEG and clipped to 120 characters',
    () async {
      final seen = <http.Request>[];
      final client = MockClient((request) async {
        seen.add(request);
        return http.Response('OK\n', 200);
      });
      final book = TerminalBook(host: '192.168.4.1', client: client);
      const jpeg = [0xFF, 0xD8, 0xFF, 0xD9];
      await book.putPhoto(
        'aabbccddeeff0011',
        1,
        jpeg,
        note: 'Sail is ripped, left hand side',
      );
      await book.putPhoto('aabbccddeeff0011', 2, jpeg, note: '   ');
      await book.putPhoto('aabbccddeeff0011', 3, jpeg, note: 'n' * 150);
      expect(seen[0].url.path, '/maintenance/photo');
      expect(seen[0].url.queryParameters['id'], 'aabbccddeeff0011');
      expect(seen[0].url.queryParameters['n'], '1');
      expect(
        seen[0].url.queryParameters['note'],
        'Sail is ripped, left hand side',
      );
      expect(seen[0].bodyBytes, jpeg);
      expect(seen[1].url.queryParameters.containsKey('note'), isFalse);
      expect(seen[2].url.queryParameters['note'], 'n' * 120);
    },
  );

  test('a blank club API does not open a connection', () async {
    var calls = 0;
    final client = MockClient((request) async {
      calls++;
      return http.Response('no', 500);
    });
    final club = ClubMaintenanceClient(rawBaseUrl: '   ', client: client);
    final book = TerminalBook(host: '192.168.4.1', client: client);
    final result = await club.uploadPending([
      {
        'id': 'aabbccddeeff0011',
        'sync': 'pending',
        'title': 'Frayed halyard',
        'sailboat_id': 504,
      },
    ], book);
    expect(club.configured, isFalse);
    expect(result.configured, isFalse);
    expect(result.reached, isFalse);
    expect(result.message, ClubMaintenanceClient.offMessage);
    expect(calls, 0);
  });

  test('a configured club posts the note and acks the boat', () async {
    final seen = <http.Request>[];
    final client = MockClient((request) async {
      seen.add(request);
      if (request.url.path == '/wp-json/lmyc/v1/boats/504/maintenance') {
        expect(request.headers['authorization'], 'Bearer member-token');
        final body = jsonDecode(request.body) as Map<String, dynamic>;
        expect(body['local_id'], 'aabbccddeeff0011');
        expect(body['title'], 'Frayed halyard');
        return http.Response('{"wp_id": 77}', 201);
      }
      if (request.url.path == '/maintenance/ack') {
        expect(request.url.queryParameters['id'], 'aabbccddeeff0011');
        expect(jsonDecode(request.body)['wp_id'], 77);
        return http.Response('{"sync":"synced"}', 200);
      }
      return http.Response('no', 404);
    });
    final club = ClubMaintenanceClient(
      rawBaseUrl: 'http://club.example/',
      token: 'member-token',
      client: client,
    );
    final book = TerminalBook(host: '192.168.4.1', client: client);
    final result = await club.uploadPending([
      {
        'id': 'aabbccddeeff0011',
        'sync': 'pending',
        'title': 'Frayed halyard',
        'description': 'at the masthead',
        'category': 'rigging',
        'priority': 'high',
        'sailboat_id': 504,
      },
      {
        'id': '1122334455667788',
        'sync': 'synced',
        'title': 'already there',
        'sailboat_id': 504,
      },
    ], book);
    expect(result.reached, isTrue);
    expect(result.uploaded, 1);
    expect(seen.map((r) => r.url.path).toList(), [
      '/wp-json/lmyc/v1/boats/504/maintenance',
      '/maintenance/ack',
    ]);
  });

  test('a missing WordPress id leaves the item pending', () async {
    final paths = <String>[];
    final client = MockClient((request) async {
      paths.add(request.url.path);
      return http.Response('{}', 200);
    });
    final club = ClubMaintenanceClient(
      rawBaseUrl: 'club.example',
      client: client,
    );
    final result = await club.uploadPending([
      {
        'id': 'aabbccddeeff0011',
        'sync': 'pending',
        'title': 'Frayed halyard',
        'sailboat_id': 504,
      },
    ], TerminalBook(host: '192.168.4.1', client: client));
    expect(result.reached, isTrue);
    expect(result.uploaded, 0);
    expect(paths, ['/wp-json/lmyc/v1/boats/504/maintenance']);
  });

  test('a club host that does not answer leaves the item pending', () async {
    final client = MockClient((request) async {
      throw http.ClientException('down', request.url);
    });
    final club = ClubMaintenanceClient(
      rawBaseUrl: 'http://club.example',
      client: client,
    );
    final result = await club.uploadPending([
      {
        'id': 'aabbccddeeff0011',
        'sync': 'pending',
        'title': 'Frayed halyard',
        'sailboat_id': 504,
      },
    ], TerminalBook(host: '192.168.4.1', client: client));
    expect(result.configured, isTrue);
    expect(result.reached, isFalse);
    expect(result.uploaded, 0);
    expect(result.message, contains('stay pending'));
  });

  test('sync with a blank club API does not open a connection', () async {
    var calls = 0;
    final client = MockClient((request) async {
      calls++;
      return http.Response('no', 500);
    });
    final result = await ClubMaintenanceClient(
      rawBaseUrl: ' ',
      client: client,
    ).syncBook(TerminalBook(host: '192.168.4.1', client: client));
    expect(result.configured, isFalse);
    expect(result.message, ClubMaintenanceClient.offMessage);
    expect(calls, 0);
  });

  test('sync brings newer club edits and leaves a pending note', () async {
    final reconciles = <int>[];
    final client = MockClient((request) async {
      if (request.method == 'GET' && request.url.path == '/maintenance') {
        return http.Response(
          jsonEncode([
            {
              'id': 'aaaaaaaaaaaaaaaa',
              'wp_id': 77,
              'sync': 'synced',
              'title': 'Old',
              'modified': '2026-09-01T00:00:00Z',
              'sailboat_id': 504,
            },
            {
              'id': 'bbbbbbbbbbbbbbbb',
              'wp_id': 78,
              'sync': 'synced',
              'title': 'Newer on the boat',
              'modified': '2026-10-01T00:00:00Z',
              'sailboat_id': 504,
            },
            {
              'id': 'cccccccccccccccc',
              'wp_id': 79,
              'sync': 'pending',
              'title': 'Not sent',
              'modified': '2026-09-01T00:00:00Z',
              'sailboat_id': 504,
            },
          ]),
          200,
        );
      }
      if (request.method == 'GET' && request.url.path == '/status.json') {
        return http.Response('{"sailboat_id":504}', 200);
      }
      if (request.method == 'POST' &&
          request.url.path == '/wp-json/lmyc/v1/boats/504/maintenance') {
        return http.Response('no', 500);
      }
      if (request.method == 'GET' &&
          request.url.path == '/wp-json/lmyc/v1/boats/504/maintenance') {
        return http.Response(
          jsonEncode([
            {
              'wp_id': 77,
              'title': 'Updated at home',
              'category': 'rigging',
              'priority': 'high',
              'status': 'in-progress',
              'modified': '2026-09-30T00:00:00Z',
              'sailboat_id': 504,
            },
            {
              'wp_id': 78,
              'title': 'Stale club copy',
              'category': 'rigging',
              'priority': 'low',
              'status': 'reported',
              'modified': '2026-09-01T00:00:00Z',
              'sailboat_id': 504,
            },
            {
              'wp_id': 79,
              'title': 'Club raced ahead',
              'category': 'engine',
              'priority': 'low',
              'status': 'reported',
              'modified': '2026-09-30T00:00:00Z',
              'sailboat_id': 504,
            },
            {
              'wp_id': 80,
              'title': 'Filed at home',
              'category': 'hull',
              'priority': 'medium',
              'status': 'reported',
              'modified': '2026-09-30T00:00:00Z',
              'sailboat_id': 504,
            },
          ]),
          200,
        );
      }
      if (request.method == 'POST' &&
          request.url.path == '/maintenance/reconcile') {
        final body = jsonDecode(request.body) as Map<String, dynamic>;
        reconciles.add(body['wp_id'] as int);
        final action = body['wp_id'] == 77 ? 'updated' : 'added';
        return http.Response(
          '{"action":"$action","id":"dddddddddddddddd"}',
          200,
        );
      }
      return http.Response('no ${request.url}', 404);
    });
    final club = ClubMaintenanceClient(
      rawBaseUrl: 'http://club.example',
      client: client,
    );
    final result = await club.syncBook(
      TerminalBook(host: '192.168.4.1', client: client),
    );
    expect(result.reached, isTrue);
    expect(result.uploaded, 0);
    expect(result.updated, 1);
    expect(result.added, 1);
    expect(reconciles, [77, 80]);
    expect(result.message, contains('Updated 1'));
    expect(result.message, contains('Added 1'));
  });

  test('a missing club list leaves the card unchanged', () async {
    final client = MockClient((request) async {
      if (request.url.path == '/maintenance') {
        return http.Response('[]', 200);
      }
      if (request.url.path == '/status.json') {
        return http.Response('{"sailboat_id":504}', 200);
      }
      throw http.ClientException('down', request.url);
    });
    final result = await ClubMaintenanceClient(
      rawBaseUrl: 'http://club.example',
      client: client,
    ).syncBook(TerminalBook(host: '192.168.4.1', client: client));
    expect(result.reached, isFalse);
    expect(result.updated, 0);
    expect(result.added, 0);
    expect(result.message, ClubMaintenanceClient.unreachableMessage);
  });

  testWidgets('the journey starts at Find with the QR first', (tester) async {
    await tester.binding.setSurfaceSize(const Size(400, 900));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    await tester.pumpWidget(const LmycApp());
    await tester.pump();
    for (final stop in ['Find', 'Connect', 'Wi-Fi', 'NMEA']) {
      expect(find.text(stop), findsOneWidget);
    }
    expect(find.text('Find your boat'), findsOneWidget);
    expect(find.widgetWithText(FilledButton, 'Scan QR code'), findsOneWidget);
    expect(
      find.widgetWithText(OutlinedButton, 'Find nearby boats'),
      findsOneWidget,
    );
    expect(find.text('Maintenance'), findsNothing);
    await tester.tap(find.byTooltip('Settings'));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('settings')), findsOneWidget);
    expect(find.text('Maintenance'), findsNothing);
    await tester.tap(find.byKey(const ValueKey('settings-diagnostics')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('diagnostics')), findsOneWidget);
    expect(find.text('Find a terminal'), findsNothing);
    expect(find.text('Maintenance book'), findsNothing);
    expect(find.text('Chart snapshot'), findsNothing);
  });

  testWidgets('the book page reads, files, and keeps the club off', (
    tester,
  ) async {
    var posts = 0;
    final client = MockClient((request) async {
      if (request.method == 'GET' && request.url.path == '/maintenance') {
        return http.Response(
          jsonEncode([
            {
              'id': 'aabbccddeeff0011',
              'title': 'Frayed halyard',
              'category': 'rigging',
              'priority': 'high',
              'status': 'reported',
              'sync': 'pending',
              'photo_count': 0,
              'sailboat_id': 504,
            },
          ]),
          200,
        );
      }
      if (request.method == 'POST' && request.url.path == '/maintenance') {
        posts++;
        final body = jsonDecode(request.body) as Map<String, dynamic>;
        expect(body['title'], 'Loose cotter pin');
        expect(body['category'], 'other');
        return http.Response('{"id":"0011223344556677","sync":"pending"}', 200);
      }
      return http.Response('no ${request.url}', 404);
    });

    await tester.binding.setSurfaceSize(const Size(400, 2400));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    await tester.pumpWidget(
      MaterialApp(home: MaintenanceBookPage(client: client)),
    );
    await tester.tap(find.text('Read book'));
    await tester.pumpAndSettle();
    expect(find.text('Frayed halyard'), findsOneWidget);

    await tester.enterText(
      find.byKey(const Key('maint-title')),
      'Loose cotter pin',
    );
    await tester.tap(find.text('Save on the boat'));
    await tester.pumpAndSettle();
    expect(posts, 1);
    expect(find.textContaining('Saved on the boat'), findsWidgets);

    final before = posts;
    await tester.tap(find.text('Sync with club'));
    await tester.pumpAndSettle();
    expect(find.text(ClubMaintenanceClient.offMessage), findsWidgets);
    expect(posts, before);
  });
}
