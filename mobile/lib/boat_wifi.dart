import 'dart:convert';

import 'package:flutter/services.dart';

import 'protocol.dart';

/// SoftAP details the terminal shares on BLE `…000040` after the OOB check.
class BoatWifiInfo {
  const BoatWifiInfo({required this.ssid, required this.psk, required this.ip});

  final String ssid;
  final String psk;
  final String ip;
}

/// Loopback address chart apps use. The relay in this app serves it.
const kRelayHost = '127.0.0.1';

/// The relay forwards this loopback port to the terminal's web server, so
/// the app's own HTTP to the boat does not go out over mobile data.
const kRelayHttpPort = 10180;

/// Where the app's HTTP to the terminal goes (maintenance book, status.json).
const kTerminalHttpHost = '$kRelayHost:$kRelayHttpPort';

/// Data older than this means the terminal went away. A quiet bus still
/// sends the terminal's heartbeat every 15 s, so allow one and a margin.
const kRelayStaleMs = 20000;

/// What the phone's networks and the chart-app relay say right now.
class BoatWifiStatus {
  const BoatWifiStatus({
    required this.onBoat,
    this.isDefault = false,
    this.relayRunning = false,
    this.relayListening = false,
    this.relayUpstream = false,
    this.relayDataAgeMs = -1,
    this.relayClients = 0,
    this.relayDeclined = false,
  });

  /// The phone has a Wi-Fi address on the boat hotspot.
  final bool onBoat;

  /// Android sends ordinary apps over the boat hotspot. Not needed: chart
  /// apps use the relay on 127.0.0.1 whatever the default network is.
  final bool isDefault;

  final bool relayRunning;

  /// Chart apps can connect to 127.0.0.1:10110.
  final bool relayListening;

  /// The relay has its connection to the terminal.
  final bool relayUpstream;

  /// Milliseconds since the terminal last sent data, -1 if it never has.
  final int relayDataAgeMs;

  /// Chart apps connected to the relay now.
  final int relayClients;

  /// The member said no to Android's Connect dialog for the boat Wi-Fi.
  final bool relayDeclined;

  /// The terminal's data is reaching the relay.
  bool get receiving =>
      relayUpstream && relayDataAgeMs >= 0 && relayDataAgeMs < kRelayStaleMs;

  /// Chart apps on this phone get the boat's data from 127.0.0.1.
  bool get ready => onBoat && relayListening && receiving;

  @override
  String toString() =>
      'BoatWifiStatus(onBoat: $onBoat, default: $isDefault, '
      'relay: ${relayListening ? 'listening' : 'off'}, '
      'upstream: $relayUpstream, receiving: $receiving, clients: $relayClients)';

  @override
  bool operator ==(Object other) =>
      other is BoatWifiStatus &&
      other.onBoat == onBoat &&
      other.isDefault == isDefault &&
      other.relayListening == relayListening &&
      other.relayUpstream == relayUpstream &&
      other.receiving == receiving &&
      other.relayClients == relayClients &&
      other.relayDeclined == relayDeclined;

  @override
  int get hashCode => Object.hash(
    onBoat,
    isDefault,
    relayListening,
    relayUpstream,
    receiving,
    relayClients,
    relayDeclined,
  );
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
  // Only an LMYC hotspot is saved, so a look-alike terminal cannot plant
  // another network name on the phone.
  if (ssid is! String || !ssid.startsWith(kLmycBlePrefix) || ssid.length > 32) {
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
  BoatWifiStatus? status,
}) {
  final ssid = info?.ssid ?? 'the boat Wi-Fi';
  final ip = info?.ip ?? '192.168.4.1';
  final LinkRow phone;
  final LinkRow charts;
  if (status == null) {
    phone = const LinkRow('Phone Wi-Fi', 'Not checked yet', LinkMark.idle);
    charts = const LinkRow('Chart apps', 'Not checked yet', LinkMark.idle);
  } else if (!status.onBoat) {
    phone = LinkRow('Phone Wi-Fi', 'Not on $ssid', LinkMark.fail);
    charts = const LinkRow(
      'Chart apps',
      'Waiting for the boat Wi-Fi',
      LinkMark.idle,
    );
  } else {
    phone = LinkRow('Phone Wi-Fi', 'On $ssid', LinkMark.done);
    if (!status.receiving) {
      charts = LinkRow(
        'Chart apps',
        'No data from the terminal at $ip yet',
        LinkMark.fail,
      );
    } else if (!status.relayListening) {
      charts = const LinkRow(
        'Chart apps',
        'Another app holds 127.0.0.1 port 10110',
        LinkMark.fail,
      );
    } else {
      charts = LinkRow(
        'Chart apps',
        '$kRelayHost port 10110, ${status.relayClients} connected',
        LinkMark.done,
      );
    }
  }
  return [
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
    phone,
    charts,
  ];
}

/// Where the phone is in getting chart apps onto the boat Wi-Fi.
enum WifiSetup {
  /// Reading the hotspot name and passphrase over Bluetooth.
  reading,

  /// The app is asking Android for the boat Wi-Fi.
  joining,

  /// On the boat Wi-Fi, but no data is reaching the relay yet.
  noAnswer,

  /// Chart apps can reach the terminal.
  ready,

  /// Something failed that the user cannot fix with a Wi-Fi setting.
  problem,
}

/// Picks the one step to show. A live [status] wins over what the app
/// remembers, so a phone that already set up this boat goes straight on.
WifiSetup wifiSetupFor({
  required bool reading,
  BoatWifiInfo? info,
  BoatWifiStatus? status,
  String? problem,
}) {
  if (reading) return WifiSetup.reading;
  if (problem != null || info == null) return WifiSetup.problem;
  if (status != null && status.relayDeclined) return WifiSetup.problem;
  if (status != null && status.onBoat) {
    return status.ready ? WifiSetup.ready : WifiSetup.noAnswer;
  }
  return WifiSetup.joining;
}

class BoatWifi {
  static const _channel = MethodChannel('ca.lmyc/boat_wifi');

  /// Where the phone stands with the boat hotspot at [ip], and the relay.
  static Future<BoatWifiStatus> status({required String ip}) async {
    final map = await _channel.invokeMapMethod<String, Object?>('status', {
      'ip': ip,
    });
    return BoatWifiStatus(
      onBoat: map?['onBoat'] == true,
      isDefault: map?['isDefault'] == true,
      relayRunning: map?['relayRunning'] == true,
      relayListening: map?['relayListening'] == true,
      relayUpstream: map?['relayUpstream'] == true,
      relayDataAgeMs: (map?['relayDataAgeMs'] as num?)?.toInt() ?? -1,
      relayClients: (map?['relayClients'] as num?)?.toInt() ?? 0,
      relayDeclined: map?['relayDeclined'] == true,
    );
  }

  /// Starts the foreground service that serves the terminal's data on
  /// 127.0.0.1:10110. Safe to call again: it keeps one connection.
  /// With [ssid] and [psk] the relay joins the boat Wi-Fi for this app only.
  static Future<void> startRelay({
    required String ip,
    String? ssid,
    String? psk,
  }) async {
    await _channel.invokeMethod<void>('startRelay', {
      'ip': ip,
      'ssid': ssid,
      'psk': psk,
    });
  }

  static Future<void> stopRelay() async {
    await _channel.invokeMethod<void>('stopRelay');
  }

  /// Opens the Wi-Fi list so the user can move the phone onto the boat.

  /// Runs [call] on whatever network the phone is using.
  ///
  /// The boat hotspot is saved for the whole phone so chart apps can reach it.
  /// There is no process binding to peel off. A club call reaches lmyc.ca only
  /// while the phone itself has an upstream route.
  static Future<T> viaInternet<T>(Future<T> Function() call) {
    return call();
  }
}
