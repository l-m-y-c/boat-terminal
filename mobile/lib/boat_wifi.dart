import 'dart:convert';

import 'package:flutter/services.dart';

/// SoftAP details the terminal shares on BLE `…000040` after the OOB check.
class BoatWifiInfo {
  const BoatWifiInfo({required this.ssid, required this.psk, required this.ip});

  final String ssid;
  final String psk;
  final String ip;
}

/// Last bootstrap from this app session. The passphrase stays in memory.
class BoatSession {
  static BoatWifiInfo? info;
  static bool joined = false;

  static String get ip => info?.ip ?? '192.168.4.1';
  static String get ssid => info?.ssid ?? '';

  static void offer(BoatWifiInfo next) {
    info = next;
    joined = false;
  }

  static void markJoined() {
    joined = info != null;
  }

  static void clear() {
    info = null;
    joined = false;
  }
}

/// Decode the terminal's Wi-Fi characteristic. Anything other than the JSON
/// object (WAIT, ERR, a short read) means the phone should not join.
BoatWifiInfo? parseWifiBootstrap(String raw) {
  final cut = raw.split('\u0000').first.trim();
  if (!cut.startsWith('{')) {
    return null;
  }
  Object? decoded;
  try {
    decoded = jsonDecode(cut);
  } catch (_) {
    return null;
  }
  if (decoded is! Map) {
    return null;
  }
  final ssid = decoded['ssid'];
  final psk = decoded['psk'];
  final ip = decoded['ip'];
  if (ssid is! String || ssid.isEmpty || ssid.length > 32) {
    return null;
  }
  if (psk is! String || psk.length < 8 || psk.length > 63) {
    return null;
  }
  if (ip is! String || ip.isEmpty || ip.length > 15) {
    return null;
  }
  return BoatWifiInfo(ssid: ssid, psk: psk, ip: ip);
}

enum LinkMark { done, wait, fail, idle }

/// One row on the phone's link card. The terminal shows the same facts.
class LinkRow {
  const LinkRow(this.name, this.value, this.mark);

  final String name;
  final String value;
  final LinkMark mark;
}

List<LinkRow> linkRows({
  required bool blePaired,
  BoatWifiInfo? info,
  required bool joining,
  required bool joined,
  bool? reached,
}) {
  final rows = <LinkRow>[
    LinkRow(
      'Bluetooth',
      blePaired ? 'Paired' : 'Not paired',
      blePaired ? LinkMark.done : LinkMark.idle,
    ),
    LinkRow(
      'Boat Wi-Fi',
      info == null ? 'Name not received' : info.ssid,
      info == null ? LinkMark.wait : LinkMark.done,
    ),
  ];
  if (joined) {
    rows.add(
      LinkRow(
        'Phone Wi-Fi',
        'On ${info?.ssid ?? 'the boat hotspot'}',
        LinkMark.done,
      ),
    );
  } else if (joining) {
    rows.add(
      const LinkRow('Phone Wi-Fi', 'Approve the Android prompt', LinkMark.wait),
    );
  } else if (info != null) {
    rows.add(
      const LinkRow('Phone Wi-Fi', 'Not on the boat hotspot', LinkMark.fail),
    );
  } else {
    rows.add(
      const LinkRow('Phone Wi-Fi', 'Waiting for the boat name', LinkMark.idle),
    );
  }
  final ip = info?.ip ?? '192.168.4.1';
  if (reached == true) {
    rows.add(LinkRow('Reach boat', '$ip port 10110 is open', LinkMark.done));
  } else if (reached == false) {
    rows.add(
      LinkRow(
        'Reach boat',
        'No route to $ip. The phone is still on another Wi-Fi network.',
        LinkMark.fail,
      ),
    );
  } else {
    rows.add(const LinkRow('Reach boat', 'Not checked yet', LinkMark.idle));
  }
  return rows;
}

class BoatWifi {
  static const _channel = MethodChannel('ca.lmyc/boat_wifi');

  /// Saves [ssid] as a normal Wi-Fi network for the whole phone.
  /// Android shows its own prompt. A phone already on home Wi-Fi must tap the boat network.
  static Future<void> join({required String ssid, required String psk}) async {
    await _channel.invokeMethod<void>('connect', {'ssid': ssid, 'psk': psk});
  }

  /// Runs [call] on whatever network the phone is using.
  ///
  /// The boat hotspot is saved for the whole phone so chart apps can reach it.
  /// There is no process binding to peel off. A club call reaches lmyc.ca only
  /// while the phone itself has an upstream route.
  static Future<T> viaInternet<T>(Future<T> Function() call) {
    return call();
  }
}
