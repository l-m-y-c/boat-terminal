import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:http/http.dart' as http;
import 'package:share_plus/share_plus.dart';

import 'diag_bundle.dart';
import 'diag_bundle_card.dart';

import 'boat_wifi.dart';
import 'event_log.dart';
import 'journey.dart';
import 'pairing_link.dart';
import 'platform_info.dart';
import 'terminal_stats.dart';
import 'ui_kit.dart';

/// Where the Bluetooth link stands, coarse enough for a verdict.
enum BleState { idle, working, paired, failed }

/// Everything Diagnostics shows, copied out of the home page at one moment.
/// Holds no passphrase: the boat Wi-Fi password never reaches this screen
/// or the report.
class DiagnosticsSnapshot {
  const DiagnosticsSnapshot({
    required this.takenAt,
    this.boat,
    this.terminalId,
    this.bleName,
    this.bleState = BleState.idle,
    this.bleLabel = 'Not connected',
    this.bleDetail = '',
    this.rssi,
    this.sessionReply,
    this.ssid,
    this.ip,
    this.wifiReading = false,
    this.wifiProblem,
    this.wifi,
    this.stats,
    this.statsAt,
    this.appVersion,
  });

  final DateTime takenAt;
  final String? boat;
  final String? terminalId;
  final String? bleName;
  final BleState bleState;
  final String bleLabel;
  final String bleDetail;
  final int? rssi;
  final String? sessionReply;
  final String? ssid;
  final String? ip;
  final bool wifiReading;
  final String? wifiProblem;
  final BoatWifiStatus? wifi;
  final TerminalStats? stats;
  final DateTime? statsAt;
  final String? appVersion;
}

/// The extra counters on the terminal's GET /status.json (hub_http.cpp).
class HubStatus {
  const HubStatus({
    required this.tcpClients,
    required this.nmeaGood,
    required this.nmeaBad,
  });

  /// TCP chart-app connections on the terminal (gateway_client_count).
  final int tcpClients;

  /// NMEA 0183 lines that passed the terminal's checksum check (nmea.cpp).
  final int nmeaGood;

  /// Lines it threw away: too short, no `*HH`, or a wrong checksum.
  final int nmeaBad;

  static HubStatus? parse(String body) {
    try {
      final d = jsonDecode(body);
      if (d is! Map) return null;
      int? n(String k) => (d[k] as num?)?.toInt();
      final good = n('nmea_good');
      final bad = n('nmea_bad');
      final tcp = n('tcp');
      if (good == null || bad == null || tcp == null) return null;
      return HubStatus(tcpClients: tcp, nmeaGood: good, nmeaBad: bad);
    } on FormatException {
      return null;
    }
  }
}

/// The one line at the top: the first thing in the chain that is wrong.
class Verdict {
  const Verdict(this.mark, this.title, this.detail);

  final LinkMark mark;
  final String title;
  final String detail;
}

Verdict diagnosticsVerdict(DiagnosticsSnapshot s) {
  final ssid = s.ssid ?? 'the boat Wi-Fi';
  final w = s.wifi;
  final st = s.stats?.state;
  if (s.bleState == BleState.failed && s.ssid == null) {
    final line = s.bleDetail.split('\n').first.trim();
    return Verdict(
      LinkMark.fail,
      'Bluetooth did not connect',
      line.isEmpty ? 'Stay close to the terminal and reconnect.' : line,
    );
  }
  if (s.bleState == BleState.working) {
    return const Verdict(
      LinkMark.wait,
      'Connecting over Bluetooth',
      'Finding the terminal and checking its pairing code.',
    );
  }
  if (s.ssid == null && s.wifiProblem == null) {
    if (s.wifiReading) {
      return const Verdict(
        LinkMark.wait,
        'Reading the boat Wi-Fi',
        'The terminal is sharing its Wi-Fi name over Bluetooth.',
      );
    }
    return const Verdict(
      LinkMark.idle,
      'Not connected to a boat',
      'Scan the terminal\'s QR code or find nearby boats.',
    );
  }
  if (s.wifiProblem != null) {
    return Verdict(LinkMark.fail, 'Boat Wi-Fi problem', s.wifiProblem!);
  }
  if (w == null) {
    return const Verdict(
      LinkMark.wait,
      'Checking the phone\'s Wi-Fi',
      'The first check runs a moment after joining.',
    );
  }
  if (w.relayDeclined) {
    return Verdict(
      LinkMark.fail,
      'Android was not allowed to join $ssid',
      'Tap Try again on the Wi-Fi step, then Connect.',
    );
  }
  if (!w.onBoat) {
    return Verdict(
      LinkMark.fail,
      'Phone is not on $ssid',
      'Android is joining it, or another network took over.',
    );
  }
  if (!w.receiving) {
    return Verdict(
      LinkMark.fail,
      'No data from the terminal',
      'On $ssid, but nothing arrived from ${s.ip ?? 'the terminal'} '
          'in the last ${kRelayStaleMs ~/ 1000} s.',
    );
  }
  if (!w.relayListening) {
    return const Verdict(
      LinkMark.fail,
      'Chart apps cannot connect',
      'Another app holds 127.0.0.1 port 10110.',
    );
  }
  if (st == BusState.busOff || st == BusState.failed) {
    return const Verdict(
      LinkMark.fail,
      'NMEA 2000 bus fault',
      'The terminal\'s CAN controller stopped. Check the backbone '
          'wiring and power.',
    );
  }
  if (st == BusState.waiting || st == BusState.quiet) {
    return const Verdict(
      LinkMark.wait,
      'Waiting for the boat\'s instruments',
      'Chart apps are connected to the terminal, but the bus is silent.',
    );
  }
  if (s.bleState != BleState.paired) {
    return const Verdict(
      LinkMark.done,
      'Chart apps are getting data',
      'Bluetooth to the terminal dropped, so the counters below stopped '
          'updating. Reconnect to see them.',
    );
  }
  return const Verdict(
    LinkMark.done,
    'All links good',
    'Chart apps on this phone are getting the boat\'s data.',
  );
}

/// A link card: a name, a mark, one headline, and the facts behind it.
class LinkCard {
  const LinkCard(
    this.key,
    this.title,
    this.icon,
    this.mark,
    this.headline,
    this.rows,
  );

  final String key;
  final String title;
  final IconData icon;
  final LinkMark mark;
  final String headline;
  final List<(String, String)> rows;
}

String _yes(bool v) => v ? 'Yes' : 'No';

String _ageText(int ms) => ms < 0
    ? 'Never'
    : ms < 1000
    ? 'Just now'
    : '${(ms / 1000).toStringAsFixed(ms < 10000 ? 1 : 0)} s ago';

List<LinkCard> diagnosticsCards(DiagnosticsSnapshot s, HubStatus? hub) {
  final ssid = s.ssid;
  final w = s.wifi;
  final stats = s.stats;

  final ble = LinkCard(
    'bluetooth',
    'Bluetooth',
    Icons.bluetooth,
    switch (s.bleState) {
      BleState.paired => LinkMark.done,
      BleState.failed => LinkMark.fail,
      BleState.working => LinkMark.wait,
      BleState.idle => LinkMark.idle,
    },
    s.bleLabel,
    [
      ('Terminal', s.bleName ?? '—'),
      if (s.terminalId != null) ('Terminal ID', s.terminalId!),
      (
        'Signal',
        s.rssi == null ? '—' : '${s.rssi} dBm · ${signalWord(s.rssi!)}',
      ),
      ('Pairing check', s.sessionReply ?? '—'),
      if (s.bleDetail.trim().isNotEmpty) ('Last step', s.bleDetail.trim()),
    ],
  );

  final boatWifi = LinkCard(
    'boat_wifi',
    'Boat Wi-Fi',
    Icons.router_outlined,
    s.wifiProblem != null
        ? LinkMark.fail
        : s.wifiReading
        ? LinkMark.wait
        : ssid != null
        ? LinkMark.done
        : LinkMark.idle,
    s.wifiProblem ??
        (s.wifiReading
            ? 'Reading over Bluetooth'
            : ssid ?? 'Name not received yet'),
    [('Network', ssid ?? '—'), ('Terminal address', s.ip ?? '—')],
  );

  final phone = LinkCard(
    'phone_wifi',
    'Phone Wi-Fi',
    Icons.wifi,
    w == null
        ? LinkMark.idle
        : w.onBoat
        ? LinkMark.done
        : LinkMark.fail,
    w == null
        ? 'Not checked yet'
        : w.onBoat
        ? 'On ${ssid ?? 'the boat Wi-Fi'}'
        : 'Not on ${ssid ?? 'the boat Wi-Fi'}',
    [
      ('On the boat Wi-Fi', w == null ? '—' : _yes(w.onBoat)),
      ('Default for other apps', w == null ? '—' : _yes(w.isDefault)),
      if (w != null && w.relayDeclined) ('Android join', 'Declined'),
    ],
  );

  final relay = LinkCard(
    'relay',
    'Relay on this phone',
    Icons.swap_horiz,
    w == null || !w.relayRunning
        ? LinkMark.idle
        : w.receiving && w.relayListening
        ? LinkMark.done
        : w.onBoat
        ? LinkMark.fail
        : LinkMark.wait,
    w == null || !w.relayRunning
        ? 'Not running'
        : !w.relayListening
        ? 'Port 10110 is taken'
        : !w.relayUpstream
        ? 'No link to the terminal'
        : !w.receiving
        ? 'Link up, no data'
        : 'Forwarding the boat\'s data',
    [
      ('Service', w == null ? '—' : (w.relayRunning ? 'Running' : 'Stopped')),
      (
        'Port 127.0.0.1:10110',
        w == null ? '—' : (w.relayListening ? 'Listening' : 'Not listening'),
      ),
      (
        'Link to terminal',
        w == null ? '—' : (w.relayUpstream ? 'Connected' : 'Not connected'),
      ),
      ('Last data', w == null ? '—' : _ageText(w.relayDataAgeMs)),
      if (w != null && w.relayNeedsApp)
        ('Wi-Fi details', 'Android restarted it without them; resending'),
    ],
  );

  final charts = LinkCard(
    'chart_apps',
    'Chart apps',
    Icons.map_outlined,
    w?.ready == true ? LinkMark.done : LinkMark.idle,
    w?.ready == true
        ? (w!.relayClients == 1
              ? '1 app connected on this phone'
              : '${w.relayClients} apps connected on this phone')
        : 'Not reachable yet',
    [
      ('On this phone', w == null ? '—' : '${w.relayClients}'),
      (
        'On the terminal',
        stats != null
            ? '${stats.chartApps} TCP of 4'
            : hub != null
            ? '${hub.tcpClients} TCP of 4'
            : '—',
      ),
      ('Sentences out', stats == null ? '—' : formatCount(stats.sentencesOut)),
      ('Not delivered', stats == null ? '—' : formatCount(stats.notDelivered)),
    ],
  );

  final bus = LinkCard(
    'bus',
    'Boat\'s data',
    Icons.cable,
    stats == null
        ? LinkMark.idle
        : switch (stats.tone) {
            Tone.good => LinkMark.done,
            Tone.caution => LinkMark.wait,
            Tone.bad => LinkMark.fail,
            Tone.neutral => LinkMark.idle,
          },
    stats?.pill ?? 'No counters yet',
    [
      ('Last frame', stats == null ? '—' : formatAge(stats.lastFrameAgeMs)),
      ('Lost frames', stats == null ? '—' : formatCount(stats.lost)),
      ('Bus errors', stats == null ? '—' : formatCount(stats.busErrors)),
      ('Terminal up', stats == null ? '—' : formatUptime(stats.uptimeSeconds)),
      if (hub != null) ...[
        ('0183 lines OK', formatCount(hub.nmeaGood)),
        ('0183 lines rejected', formatCount(hub.nmeaBad)),
      ],
      (
        'Counters read',
        s.statsAt == null
            ? 'Not yet'
            : _ageText(s.takenAt.difference(s.statsAt!).inMilliseconds),
      ),
    ],
  );

  return [ble, boatWifi, phone, relay, charts, bus];
}

/// What each card means, from the code that fills it in.
const diagnosticsHelp = <String, (String, List<String>)>{
  'verdict': (
    'The verdict',
    [
      'The app walks the chain from Bluetooth to the chart apps and names '
          'the first link that is not right. The cards below show each link.',
      'Bluetooth is only needed to join and to read the counters. Once on the '
          'boat Wi-Fi, chart apps keep working without it.',
    ],
  ),
  'bluetooth': (
    'Bluetooth',
    [
      'The link to the terminal used to join: the phone finds the terminal, '
          'reads its pairing code and writes it back. Pairing check shows the '
          'terminal\'s answer; OK means it accepted this phone.',
      'Signal is the strength of the terminal\'s advertisement when it was '
          'last heard. Below -80 dBm, move closer.',
      'After joining, the app reads the terminal\'s counters over Bluetooth '
          'every 2 s. If Bluetooth drops, chart apps keep working; only the '
          'counters stop.',
    ],
  ),
  'boat_wifi': (
    'Boat Wi-Fi',
    [
      'The terminal runs its own Wi-Fi network. It sends its name and '
          'password over Bluetooth after the pairing check. The password '
          'stays in the app\'s memory and is never shown or shared.',
      'Terminal address is where the terminal listens on that network, '
          'normally 192.168.4.1.',
    ],
  ),
  'phone_wifi': (
    'Phone Wi-Fi',
    [
      'Whether Android has the phone on the boat Wi-Fi. The app asks Android '
          'to join it for this app only, so other apps can stay on mobile '
          'data.',
      'Default for other apps is Yes when Android sends everything over the '
          'boat Wi-Fi. Chart apps do not need it: they use the relay on '
          '127.0.0.1 whatever the default is.',
      'Checked every 2 s while the app is open.',
    ],
  ),
  'relay': (
    'Relay on this phone',
    [
      'A background service in this app connects to the terminal over the '
          'boat Wi-Fi and serves the data on 127.0.0.1 port 10110, which is '
          'where chart apps on this phone read it. It shows a notification '
          'while it runs.',
      'Port taken means another app already listens on 10110; close it or '
          'restart the phone.',
      'The terminal sends something at least every 15 s, even on a quiet '
          'bus. Last data older than ${kRelayStaleMs ~/ 1000} s counts as '
          'no data.',
    ],
  ),
  'chart_apps': (
    'Chart apps',
    [
      'On this phone counts apps connected to the relay. This app\'s own '
          'Instruments screen is one of them while it is open.',
      'On the terminal counts TCP connections to the terminal itself, at '
          'most 4. This phone\'s relay is one of them.',
      'Sentences out and Not delivered are the terminal\'s counters; tap them '
          'on the Network tab for the details.',
    ],
  ),
  'bus': (
    'Boat\'s data',
    [
      'The terminal\'s view of the boat\'s NMEA 2000 bus, read over '
          'Bluetooth every 2 s: whether data is flowing, how long since the '
          'last frame, frames lost and bus errors since the terminal started.',
      '0183 lines OK and rejected come from the terminal\'s web page over the '
          'boat Wi-Fi. They count every NMEA 0183 line the terminal built or '
          'received and checked; rejected lines had no checksum or a wrong '
          'one and were not sent on.',
      'Counters read shows how fresh these numbers are. They stop when '
          'Bluetooth drops.',
    ],
  ),
  'events': (
    'Recent events',
    [
      'The last 200 things this app noticed: Bluetooth steps, Wi-Fi and relay '
          'changes, errors. Newest first. Kept in memory only and cleared '
          'when the app closes.',
      'They are included when you copy or share the report.',
    ],
  ),
};

void showDiagnosticsHelp(BuildContext context, String key) {
  final help = diagnosticsHelp[key];
  if (help == null) return;
  unawaited(
    showInfoSheet(
      context,
      title: help.$1,
      icon: Icons.help_outline,
      paragraphs: help.$2,
    ),
  );
}

String _markWord(LinkMark m) => switch (m) {
  LinkMark.done => 'OK',
  LinkMark.wait => 'WAIT',
  LinkMark.fail => 'FAIL',
  LinkMark.idle => '--',
};

/// Plain text for a message or a bug report. No passphrase, no pairing code.
String diagnosticsReport(
  DiagnosticsSnapshot s, {
  HubStatus? hub,
  List<LogEvent> events = const [],
}) {
  final out = StringBuffer();
  final v = diagnosticsVerdict(s);
  out.writeln('LMYC boat terminal: diagnostics');
  out.writeln('Taken ${s.takenAt.toIso8601String()}');
  if (s.appVersion != null) out.writeln('App ${s.appVersion}');
  out.writeln('Boat ${s.boat ?? '—'}');
  out.writeln();
  out.writeln('[${_markWord(v.mark)}] ${v.title}');
  out.writeln(v.detail);
  for (final c in diagnosticsCards(s, hub)) {
    out.writeln();
    out.writeln('[${_markWord(c.mark)}] ${c.title}: ${c.headline}');
    for (final (label, value) in c.rows) {
      out.writeln('  $label: ${value.replaceAll('\n', ' / ')}');
    }
  }
  out.writeln();
  out.writeln('Recent events (${events.length}, oldest first)');
  for (final e in events) {
    out.writeln('  ${clockTime(e.at)} ${e.text.replaceAll('\n', ' / ')}');
  }
  return out.toString();
}

Color markColor(LinkMark m) => switch (m) {
  LinkMark.done => kGreen,
  LinkMark.wait => kAmber,
  LinkMark.fail => kRed,
  LinkMark.idle => const Color(0xFF5C6B7A),
};

IconData _markIcon(LinkMark m) => switch (m) {
  LinkMark.done => Icons.check_circle,
  LinkMark.wait => Icons.schedule,
  LinkMark.fail => Icons.error,
  LinkMark.idle => Icons.remove_circle_outline,
};

/// The Diagnostics screen. A tab on board; a pushed page before that.
class DiagnosticsView extends StatefulWidget {
  const DiagnosticsView({
    super.key,
    required this.snapshot,
    this.log,
    this.hubFetch,
    this.diag,
    this.onReconnect,
    this.onSentences,
    this.onForget,
    this.forgetLabel,
  });

  final DiagnosticsSnapshot snapshot;
  final EventLog? log;

  /// Reads GET /status.json through the relay. Polled every 5 s while the
  /// phone is on the boat Wi-Fi.
  final Future<HubStatus?> Function()? hubFetch;

  /// Collect diagnostics (zip bundle). Null hides the card.
  final DiagBundleController? diag;

  final VoidCallback? onReconnect;
  final VoidCallback? onSentences;
  final Future<void> Function()? onForget;

  /// The boat the app would rejoin, shown on the Forget action.
  final String? forgetLabel;

  @override
  State<DiagnosticsView> createState() => _DiagnosticsViewState();
}

class _DiagnosticsViewState extends State<DiagnosticsView> {
  HubStatus? _hub;
  Timer? _hubTimer;
  bool _hubReading = false;
  bool _allEvents = false;

  EventLog get _log => widget.log ?? EventLog.instance;

  @override
  void initState() {
    super.initState();
    _log.addListener(_onLog);
    if (widget.hubFetch != null) {
      unawaited(_readHub());
      _hubTimer = Timer.periodic(
        const Duration(seconds: 5),
        (_) => unawaited(_readHub()),
      );
    }
  }

  void _onLog() {
    if (mounted) setState(() {});
  }

  Future<void> _readHub() async {
    final fetch = widget.hubFetch;
    if (fetch == null || _hubReading) return;
    if (widget.snapshot.wifi?.ready != true) return;
    _hubReading = true;
    try {
      final next = await fetch();
      if (mounted && next != null) setState(() => _hub = next);
    } finally {
      _hubReading = false;
    }
  }

  @override
  void dispose() {
    _hubTimer?.cancel();
    _log.removeListener(_onLog);
    super.dispose();
  }

  String get _report =>
      diagnosticsReport(widget.snapshot, hub: _hub, events: _log.events);

  Future<void> _copy() async {
    await Clipboard.setData(ClipboardData(text: _report));
    // Android 13+ shows its own "Copied" confirmation; don't double it.
    if (await systemShowsCopyToast()) return;
    if (!mounted) return;
    ScaffoldMessenger.of(context)
        .showSnackBar(const SnackBar(content: Text('Report copied')));
  }

  Future<void> _share() async {
    try {
      await SharePlus.instance.share(
        ShareParams(text: _report, subject: 'LMYC terminal diagnostics'),
      );
    } catch (_) {
      await _copy();
    }
  }

  Future<void> _forget() async {
    final forget = widget.onForget;
    if (forget == null) return;
    final ok = await showDialog<bool>(
      context: context,
      builder: (d) => AlertDialog(
        title: const Text('Forget this boat?'),
        content: Text(
          'The app will stop joining ${widget.forgetLabel ?? 'it'} by itself '
          'when it opens. You can join again by scanning its QR code or '
          'finding nearby boats.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(d).pop(false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(d).pop(true),
            child: const Text('Forget'),
          ),
        ],
      ),
    );
    if (ok == true) await forget();
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.snapshot;
    final p = Palette.of(context);
    final verdict = diagnosticsVerdict(s);
    final cards = diagnosticsCards(s, _hub);
    final events = _log.events.reversed.toList();
    final shown = _allEvents ? events : events.take(12).toList();
    return ListView(
      key: const ValueKey('diagnostics'),
      padding: const EdgeInsets.fromLTRB(20, 14, 20, 28),
      children: [
        _VerdictCard(verdict: verdict),
        const SizedBox(height: 12),
        Wrap(
          spacing: 10,
          runSpacing: 10,
          children: [
            if (widget.onReconnect != null)
              FilledButton.tonalIcon(
                key: const ValueKey('diag-reconnect'),
                onPressed: widget.onReconnect,
                icon: const Icon(Icons.bluetooth_searching),
                label: const Text('Reconnect Bluetooth'),
              ),
            if (widget.onSentences != null)
              FilledButton.tonalIcon(
                key: const ValueKey('diag-sentences'),
                onPressed: widget.onSentences,
                icon: const Icon(Icons.ssid_chart),
                label: const Text('Live sentences'),
              ),
            FilledButton.tonalIcon(
              key: const ValueKey('diag-copy'),
              onPressed: () => unawaited(_copy()),
              icon: const Icon(Icons.copy_all),
              label: const Text('Copy report'),
            ),
            FilledButton.tonalIcon(
              key: const ValueKey('diag-share'),
              onPressed: () => unawaited(_share()),
              icon: const Icon(Icons.ios_share),
              label: const Text('Share report'),
            ),
            if (widget.onForget != null)
              OutlinedButton.icon(
                key: const ValueKey('diag-forget'),
                onPressed: () => unawaited(_forget()),
                style: OutlinedButton.styleFrom(
                  foregroundColor:
                      Theme.of(context).brightness == Brightness.dark
                      ? kRedOnDark
                      : kRed,
                ),
                icon: const Icon(Icons.delete_outline),
                label: Text(
                  widget.forgetLabel == null
                      ? 'Forget this boat'
                      : 'Forget ${widget.forgetLabel}',
                ),
              ),
          ],
        ),
        if (widget.diag != null) ...[
          const SizedBox(height: 14),
          DiagBundleCard(controller: widget.diag!),
        ],
        const SizedBox(height: 18),
        const SectionLabel('Links'),
        const SizedBox(height: 10),
        for (final c in cards) ...[
          _LinkCardView(card: c),
          const SizedBox(height: 10),
        ],
        const SizedBox(height: 22),
        Row(
          children: [
            const Expanded(child: SectionLabel('Recent events')),
            IconButton(
              tooltip: 'About recent events',
              visualDensity: VisualDensity.compact,
              onPressed: () => showDiagnosticsHelp(context, 'events'),
              icon: Icon(Icons.info_outline, size: 18, color: p.faint),
            ),
          ],
        ),
        const SizedBox(height: 4),
        Container(
          key: const ValueKey('diag-events'),
          padding: const EdgeInsets.fromLTRB(14, 10, 14, 10),
          decoration: BoxDecoration(
            color: p.card,
            borderRadius: BorderRadius.circular(14),
            border: Border.all(color: p.line),
          ),
          child: events.isEmpty
              ? Text('Nothing yet.', style: TextStyle(color: p.muted))
              : Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    for (final e in shown)
                      Padding(
                        padding: const EdgeInsets.symmetric(vertical: 3),
                        child: Row(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(
                              clockTime(e.at),
                              style: TextStyle(
                                fontSize: 12,
                                fontFeatures: const [
                                  FontFeature.tabularFigures(),
                                ],
                                color: p.faint,
                              ),
                            ),
                            const SizedBox(width: 10),
                            Expanded(
                              child: Text(
                                e.text,
                                style: TextStyle(
                                  fontSize: 12.5,
                                  height: 1.35,
                                  color: p.body,
                                ),
                              ),
                            ),
                          ],
                        ),
                      ),
                    if (events.length > shown.length || _allEvents)
                      TextButton(
                        onPressed: () =>
                            setState(() => _allEvents = !_allEvents),
                        child: Text(
                          _allEvents
                              ? 'Show fewer'
                              : 'Show all ${events.length}',
                        ),
                      ),
                  ],
                ),
        ),
      ],
    );
  }
}

class _VerdictCard extends StatelessWidget {
  const _VerdictCard({required this.verdict});

  final Verdict verdict;

  @override
  Widget build(BuildContext context) {
    final color = markColor(verdict.mark);
    final p = Palette.of(context);
    final shape = RoundedRectangleBorder(
      borderRadius: BorderRadius.circular(18),
      side: BorderSide(color: color.withValues(alpha: 0.35)),
    );
    return Material(
      color: Color.alphaBlend(color.withValues(alpha: 0.09), p.card),
      shape: shape,
      child: InkWell(
        key: const ValueKey('diag-verdict'),
        customBorder: shape,
        onTap: () => showDiagnosticsHelp(context, 'verdict'),
        child: Padding(
          padding: const EdgeInsets.fromLTRB(16, 16, 12, 16),
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Icon(_markIcon(verdict.mark), color: color, size: 30),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      verdict.title,
                      style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.w700,
                        color: p.ink,
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      verdict.detail,
                      style: TextStyle(color: p.body, height: 1.4),
                    ),
                  ],
                ),
              ),
              Icon(Icons.info_outline, size: 18, color: p.faint),
            ],
          ),
        ),
      ),
    );
  }
}

class _LinkCardView extends StatelessWidget {
  const _LinkCardView({required this.card});

  final LinkCard card;

  @override
  Widget build(BuildContext context) {
    final p = Palette.of(context);
    final color = markColor(card.mark);
    final shape = RoundedRectangleBorder(
      borderRadius: BorderRadius.circular(14),
      side: BorderSide(color: p.line),
    );
    return Semantics(
      button: true,
      hint: 'What is ${card.title}?',
      child: Material(
        color: p.card,
        shape: shape,
        child: InkWell(
          key: ValueKey('diag-${card.key}'),
          customBorder: shape,
          onTap: () => showDiagnosticsHelp(context, card.key),
          child: Padding(
            padding: const EdgeInsets.fromLTRB(14, 12, 12, 12),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Icon(card.icon, size: 20, color: p.muted),
                    const SizedBox(width: 8),
                    Expanded(
                      child: Text(
                        card.title,
                        style: TextStyle(
                          fontWeight: FontWeight.w600,
                          color: p.muted,
                        ),
                      ),
                    ),
                    Icon(_markIcon(card.mark), size: 18, color: color),
                    const SizedBox(width: 6),
                    Icon(Icons.info_outline, size: 16, color: p.faint),
                  ],
                ),
                const SizedBox(height: 6),
                Text(
                  card.headline,
                  style: TextStyle(
                    fontSize: 16.5,
                    fontWeight: FontWeight.w700,
                    color: card.mark == LinkMark.fail ? kRed : p.ink,
                  ),
                ),
                const SizedBox(height: 8),
                for (final (label, value) in card.rows)
                  Padding(
                    padding: const EdgeInsets.symmetric(vertical: 2),
                    child: Row(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        SizedBox(
                          width: 138,
                          child: Text(
                            label,
                            style: TextStyle(fontSize: 13, color: p.muted),
                          ),
                        ),
                        Expanded(
                          child: Text(
                            value,
                            style: TextStyle(
                              fontSize: 13,
                              color: p.body,
                              fontFeatures: const [
                                FontFeature.tabularFigures(),
                              ],
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// GET /status.json through the relay's loopback port, so it goes over the
/// boat Wi-Fi rather than mobile data. Null when the terminal is not there.
Future<HubStatus?> readHubStatus({http.Client? client}) async {
  final c = client ?? http.Client();
  try {
    final res = await c
        .get(Uri.parse('http://$kTerminalHttpHost/status.json'))
        .timeout(const Duration(seconds: 3));
    if (res.statusCode != 200) return null;
    return HubStatus.parse(res.body);
  } catch (_) {
    return null;
  } finally {
    if (client == null) c.close();
  }
}
