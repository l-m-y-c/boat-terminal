import 'dart:convert';
import 'dart:typed_data';

import 'package:http/http.dart' as http;

import 'protocol.dart';
import 'utf8_limit.dart';

/// Host[:port] for the terminal, or for the stand-in on this computer.
/// A pasted `http://` prefix is stripped. The boat speaks plain HTTP on the LAN.
String terminalHost(String raw) {
  var host = raw.trim();
  if (host.startsWith('http://')) {
    host = host.substring('http://'.length);
  } else if (host.startsWith('https://')) {
    host = host.substring('https://'.length);
  }
  while (host.endsWith('/')) {
    host = host.substring(0, host.length - 1);
  }
  return host;
}

/// Club base URL. Blank means the JSON API is not in use.
String clubBaseUrl(String raw) {
  var base = raw.trim();
  while (base.endsWith('/')) {
    base = base.substring(0, base.length - 1);
  }
  if (base.isEmpty) {
    return '';
  }
  if (!base.contains('://')) {
    base = 'http://$base';
  }
  if (base == 'http://' || base == 'https://') {
    return '';
  }
  return base;
}

/// The terminal stores [kMaintPhotoNoteMax] UTF-8 bytes beside each photo.
/// The boat's HTTP server accepts a URI of 512 bytes, and
/// `/maintenance/photo?id=&n=&note=` already uses about 50 of those, so a
/// heavily encoded note is shortened.
String _clipPhotoNote(String note) {
  var text = clipUtf8(note.trim(), kMaintPhotoNoteMax);
  while (text.isNotEmpty && Uri.encodeQueryComponent(text).length > 450) {
    // Drop a whole character, never half of a surrogate pair.
    final runes = text.runes.toList()..removeLast();
    text = String.fromCharCodes(runes);
  }
  return text;
}

/// The card's `/maintenance` routes. The caller owns [client].
class TerminalBook {
  TerminalBook({
    required this.host,
    http.Client? client,
    this.timeout = const Duration(seconds: 20),
  }) : _client = client ?? http.Client();

  final String host;
  final Duration timeout;
  final http.Client _client;

  Uri _uri(String path, [Map<String, String>? query]) =>
      Uri.http(host, path, query);

  Future<List<Map<String, dynamic>>> list() async {
    final res = await _client.get(_uri('/maintenance')).timeout(timeout);
    if (res.statusCode != 200) {
      throw Exception('HTTP ${res.statusCode}');
    }
    final decoded = jsonDecode(res.body);
    if (decoded is! List) {
      throw Exception('Expected a list');
    }
    return [
      for (final row in decoded)
        if (row is Map) Map<String, dynamic>.from(row),
    ];
  }

  /// Returns the 16-hex id stored on the card. The item stays `pending`.
  Future<String> create({
    required String title,
    required String description,
    required String category,
    required String priority,
  }) async {
    final res = await _client
        .post(
          _uri('/maintenance'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode({
            'title': title,
            'description': description,
            'category': category,
            'priority': priority,
          }),
        )
        .timeout(timeout);
    if (res.statusCode != 200) {
      throw Exception('HTTP ${res.statusCode} ${res.body}');
    }
    final decoded = jsonDecode(res.body);
    if (decoded is! Map || decoded['id'] == null) {
      throw Exception('The boat did not return an id');
    }
    return decoded['id'].toString();
  }

  /// [n] is 1-based. [jpeg] must already be a JPEG under 500 KB.
  /// [note] is the line that belongs to this photo, such as
  /// "Sail is ripped, left hand side". Blank notes are omitted.
  Future<void> putPhoto(
    String id,
    int n,
    List<int> jpeg, {
    String note = '',
  }) async {
    final query = <String, String>{'id': id, 'n': '$n'};
    final clipped = _clipPhotoNote(note);
    if (clipped.isNotEmpty) {
      query['note'] = clipped;
    }
    final res = await _client
        .post(
          _uri('/maintenance/photo', query),
          headers: {'Content-Type': 'image/jpeg'},
          body: jpeg,
        )
        .timeout(timeout);
    if (res.statusCode != 200) {
      throw Exception('Photo $n HTTP ${res.statusCode}');
    }
  }

  Future<Uint8List> getPhoto(String id, int n) async {
    final res = await _client
        .get(_uri('/maintenance/photo', {'id': id, 'n': '$n'}))
        .timeout(timeout);
    if (res.statusCode != 200) {
      throw Exception('Photo $n HTTP ${res.statusCode}');
    }
    return res.bodyBytes;
  }

  /// Sailboat id stamped by this terminal, when `/status.json` provides it.
  Future<int?> sailboatId() async {
    try {
      final res = await _client.get(_uri('/status.json')).timeout(timeout);
      if (res.statusCode != 200) {
        return null;
      }
      final decoded = jsonDecode(res.body);
      if (decoded is! Map) {
        return null;
      }
      final raw = decoded['sailboat_id'];
      if (raw is int) {
        return raw;
      }
      return int.tryParse(raw?.toString() ?? '');
    } on Exception {
      return null;
    }
  }

  /// Apply one club item. Returns `added`, `updated`, `kept`, or `skipped`.
  Future<String> reconcile(Map<String, dynamic> clubItem) async {
    final res = await _client
        .post(
          _uri('/maintenance/reconcile'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode({
            'wp_id': clubItem['wp_id'],
            'sailboat_id': clubItem['sailboat_id'] ?? 0,
            'title': clubItem['title'],
            'description': clubItem['description'] ?? '',
            'category': clubItem['category'],
            'priority': clubItem['priority'],
            'status': clubItem['status'] ?? '',
            'reporter': clubItem['reporter'] ?? '',
            'created': clubItem['created'] ?? '',
            'modified': clubItem['modified'] ?? '',
          }),
        )
        .timeout(timeout);
    if (res.statusCode != 200) {
      throw Exception('Reconcile HTTP ${res.statusCode}');
    }
    final decoded = jsonDecode(res.body);
    if (decoded is! Map || decoded['action'] == null) {
      throw Exception('The boat did not apply the club edit');
    }
    return decoded['action'].toString();
  }

  /// Marks the card copy synced after the club accepted [wpId].
  Future<void> ack(String id, int wpId) async {
    final res = await _client
        .post(
          _uri('/maintenance/ack', {'id': id}),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode({'wp_id': wpId}),
        )
        .timeout(timeout);
    if (res.statusCode != 200) {
      throw Exception('Ack HTTP ${res.statusCode}');
    }
  }
}

class ClubSyncResult {
  const ClubSyncResult({
    required this.configured,
    required this.reached,
    required this.uploaded,
    required this.updated,
    required this.added,
    required this.message,
  });

  final bool configured;
  final bool reached;
  final int uploaded;
  final int updated;
  final int added;
  final String message;
}

class ClubUploadResult {
  const ClubUploadResult({
    required this.configured,
    required this.reached,
    required this.uploaded,
    required this.message,
  });

  final bool configured;
  final bool reached;
  final int uploaded;
  final String message;
}

/// Future `POST /wp-json/lmyc/v1/boats/{sailboat_id}/maintenance`.
///
/// An empty [baseUrl] does not open a connection. A missing host leaves every
/// item `pending` on the boat. A 200 or 201 with `wp_id` is acknowledged on
/// the terminal. Anything else stays pending.
/// Wraps one club request. The app passes `BoatWifi.viaInternet` so club
/// calls leave the boat SoftAP. Tests and the stand-in use [directRoute].
typedef ClubRoute = Future<T> Function<T>(Future<T> Function() call);

Future<T> directRoute<T>(Future<T> Function() call) => call();

class ClubMaintenanceClient {
  ClubMaintenanceClient({
    required String rawBaseUrl,
    this.token = '',
    http.Client? client,
    this.timeout = const Duration(seconds: 12),
    this.route = directRoute,
  }) : baseUrl = clubBaseUrl(rawBaseUrl),
       _client = client ?? http.Client();

  static const offMessage = 'Club API is off. The copy stays on the boat.';
  static const unreachableMessage =
      'Club host did not answer. Items stay pending on the boat.';

  final String baseUrl;
  final String token;
  final Duration timeout;
  final ClubRoute route;
  final http.Client _client;

  bool get configured => baseUrl.isNotEmpty;

  Future<ClubUploadResult> uploadPending(
    List<Map<String, dynamic>> items,
    TerminalBook book,
  ) async {
    if (!configured) {
      return const ClubUploadResult(
        configured: false,
        reached: false,
        uploaded: 0,
        message: offMessage,
      );
    }
    final pending = [
      for (final item in items)
        if (item['sync']?.toString() == 'pending' &&
            (item['id']?.toString().isNotEmpty ?? false))
          item,
    ];
    if (pending.isEmpty) {
      return const ClubUploadResult(
        configured: true,
        reached: false,
        uploaded: 0,
        message: 'Nothing pending on this boat.',
      );
    }

    var reached = false;
    var uploaded = 0;
    for (final item in pending) {
      final localId = item['id'].toString();
      final sailboatId = item['sailboat_id'] ?? 0;
      final uri = Uri.parse(
        '$baseUrl/wp-json/lmyc/v1/boats/$sailboatId/maintenance',
      );
      http.Response res;
      try {
        res = await route(
          () => _client
              .post(
                uri,
                headers: {
                  'Content-Type': 'application/json',
                  if (token.isNotEmpty) 'Authorization': 'Bearer $token',
                },
                body: jsonEncode({
                  'local_id': localId,
                  'title': item['title'],
                  'description': item['description'],
                  'category': item['category'],
                  'priority': item['priority'],
                }),
              )
              .timeout(timeout),
        );
      } on Exception {
        return ClubUploadResult(
          configured: true,
          reached: reached,
          uploaded: uploaded,
          message: reached
              ? 'The club answered, then the connection dropped. '
                    '$uploaded saved. The rest stay pending on the boat.'
              : unreachableMessage,
        );
      }
      reached = true;
      if (res.statusCode != 200 && res.statusCode != 201) {
        continue;
      }
      final wpId = _wpId(res.body);
      if (wpId == null) {
        continue;
      }
      try {
        await book.ack(localId, wpId);
      } on Exception {
        return ClubUploadResult(
          configured: true,
          reached: true,
          uploaded: uploaded,
          message:
              'The club accepted the note, and the boat did not confirm it. '
              'It stays pending so it can be tried again.',
        );
      }
      uploaded++;
    }
    if (uploaded == 0) {
      return const ClubUploadResult(
        configured: true,
        reached: true,
        uploaded: 0,
        message:
            'The club answered without a WordPress id. Items stay pending.',
      );
    }
    return ClubUploadResult(
      configured: true,
      reached: true,
      uploaded: uploaded,
      message: uploaded == pending.length
          ? 'Sent $uploaded to the club. Photos stay on the boat.'
          : 'Sent $uploaded. The rest stay pending on the boat.',
    );
  }

  /// Upload pending notes, then bring home edits back onto the card.
  ///
  /// The website's `modified` time wins. A note that is still `pending` is
  /// not replaced. An empty [baseUrl] does not open a connection.
  Future<ClubSyncResult> syncBook(TerminalBook book) async {
    if (!configured) {
      return const ClubSyncResult(
        configured: false,
        reached: false,
        uploaded: 0,
        updated: 0,
        added: 0,
        message: offMessage,
      );
    }
    List<Map<String, dynamic>> local;
    try {
      local = await book.list();
    } on Exception {
      return const ClubSyncResult(
        configured: true,
        reached: false,
        uploaded: 0,
        updated: 0,
        added: 0,
        message: 'Could not read the boat before asking the club.',
      );
    }
    final upload = await uploadPending(local, book);
    if (upload.message == unreachableMessage ||
        upload.message.startsWith('The club answered, then')) {
      return ClubSyncResult(
        configured: true,
        reached: upload.reached,
        uploaded: upload.uploaded,
        updated: 0,
        added: 0,
        message: upload.message,
      );
    }

    List<Map<String, dynamic>> fresh;
    try {
      fresh = await book.list();
    } on Exception {
      return ClubSyncResult(
        configured: true,
        reached: upload.reached,
        uploaded: upload.uploaded,
        updated: 0,
        added: 0,
        message: upload.uploaded == 0
            ? 'Could not re-read the boat after the club answered.'
            : 'Sent ${upload.uploaded}. Could not re-read the boat.',
      );
    }

    final boatIds = <int>{};
    final stamped = await book.sailboatId();
    if (stamped != null) {
      boatIds.add(stamped);
    }
    for (final item in fresh) {
      boatIds.add(int.tryParse('${item['sailboat_id']}') ?? 0);
    }
    if (boatIds.isEmpty) {
      boatIds.add(0);
    }

    final byWp = <int, Map<String, dynamic>>{};
    for (final item in fresh) {
      final wp = int.tryParse('${item['wp_id']}') ?? 0;
      if (wp > 0) {
        byWp[wp] = item;
      }
    }

    var reached = upload.reached;
    var updated = 0;
    var added = 0;
    for (final sailboatId in boatIds) {
      List<Map<String, dynamic>> clubItems;
      try {
        clubItems = await _fetchBoat(sailboatId);
        reached = true;
      } on Exception {
        return ClubSyncResult(
          configured: true,
          reached: reached,
          uploaded: upload.uploaded,
          updated: updated,
          added: added,
          message: reached
              ? 'Sent ${upload.uploaded}. Club edits could not be read.'
              : unreachableMessage,
        );
      }
      for (final clubItem in clubItems) {
        final wp = int.tryParse('${clubItem['wp_id']}') ?? 0;
        if (wp <= 0) {
          continue;
        }
        final card = byWp[wp];
        if (card != null && card['sync']?.toString() == 'pending') {
          continue;
        }
        if (card != null) {
          final clubModified = clubItem['modified']?.toString() ?? '';
          final cardModified = card['modified']?.toString() ?? '';
          if (clubModified.isEmpty ||
              (cardModified.isNotEmpty &&
                  clubModified.compareTo(cardModified) <= 0)) {
            continue;
          }
        }
        try {
          final action = await book.reconcile(clubItem);
          if (action == 'updated') {
            updated++;
          } else if (action == 'added') {
            added++;
          }
        } on Exception {
          return ClubSyncResult(
            configured: true,
            reached: true,
            uploaded: upload.uploaded,
            updated: updated,
            added: added,
            message:
                'The club answered. The boat did not store the edit. '
                'Sent ${upload.uploaded}, updated $updated, added $added.',
          );
        }
      }
    }

    final parts = <String>[
      if (upload.uploaded > 0) 'Sent ${upload.uploaded} to the club.',
      if (updated > 0) 'Updated $updated from the club.',
      if (added > 0) 'Added $added from the club.',
    ];
    return ClubSyncResult(
      configured: true,
      reached: true,
      uploaded: upload.uploaded,
      updated: updated,
      added: added,
      message: parts.isEmpty ? 'Club book matches this boat.' : parts.join(' '),
    );
  }

  Future<List<Map<String, dynamic>>> _fetchBoat(int sailboatId) async {
    final res = await route(
      () => _client
          .get(
            Uri.parse('$baseUrl/wp-json/lmyc/v1/boats/$sailboatId/maintenance'),
            headers: {if (token.isNotEmpty) 'Authorization': 'Bearer $token'},
          )
          .timeout(timeout),
    );
    if (res.statusCode != 200) {
      throw Exception('HTTP ${res.statusCode}');
    }
    final decoded = jsonDecode(res.body);
    if (decoded is! List) {
      throw Exception('Expected a list');
    }
    return [
      for (final row in decoded)
        if (row is Map) Map<String, dynamic>.from(row),
    ];
  }

  static int? _wpId(String body) {
    try {
      final decoded = jsonDecode(body);
      if (decoded is! Map) {
        return null;
      }
      final raw = decoded['wp_id'];
      final id = raw is int ? raw : int.tryParse(raw?.toString() ?? '');
      if (id == null || id <= 0) {
        return null;
      }
      return id;
    } on FormatException {
      return null;
    }
  }
}
