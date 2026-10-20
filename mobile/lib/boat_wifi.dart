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

class BoatWifi {
  static const _channel = MethodChannel('ca.lmyc/boat_wifi');

  /// Asks Android to join [ssid]. The system shows a Connect prompt.
  /// On success this app's sockets use that network.
  static Future<void> join({required String ssid, required String psk}) async {
    await _channel.invokeMethod<void>('connect', {'ssid': ssid, 'psk': psk});
  }

  static Future<void> release() async {
    try {
      await _channel.invokeMethod<void>('release');
    } catch (_) {}
    BoatSession.joined = false;
  }

  /// Runs [call] on the phone's internet route, then binds back to the boat.
  ///
  /// Joining binds every socket in this app to the boat SoftAP, which has no
  /// internet, so club calls need this. The boat Wi-Fi stays joined. Boat
  /// calls made while [call] runs would go the wrong way, so keep club work
  /// sequential with boat work, as `syncBook` does.
  static Future<T> viaInternet<T>(Future<T> Function() call) async {
    if (!BoatSession.joined) {
      return call();
    }
    try {
      await _channel.invokeMethod<void>('useInternet');
    } catch (_) {}
    try {
      return await call();
    } finally {
      try {
        await _channel.invokeMethod<bool>('useBoat');
      } catch (_) {}
    }
  }
}
