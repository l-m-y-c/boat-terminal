/// Network: the NMEA 2000 topology the terminal discovers, and one device in
/// detail. GET /n2k/network.json and /n2k/device.json?src=N
/// (firmware/src/n2k_network.cpp). Same picture as the terminal's Network
/// screen.
library;

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'package:share_plus/share_plus.dart';

import 'boat_wifi.dart';
import 'bus_check.dart';
import 'journey.dart' show kNavy;
import 'nmea_data_page.dart' show toneColor;
import 'terminal_stats.dart';
import 'ui_kit.dart';

int _int(Object? v, [int def = 0]) => v is num ? v.toInt() : def;
String _str(Object? v) => v is String ? v : '';

class NetIssue {
  const NetIssue(this.code, this.text);
  final String code;
  final String text;
}

/// One device on the bus.
class NetDevice {
  const NetDevice({
    required this.address,
    required this.self,
    required this.title,
    required this.role,
    required this.deviceClass,
    required this.function,
    required this.ageMs,
    required this.complete,
    required this.issues,
    this.maker = '',
    this.manufacturerCode,
    this.instance,
    this.systemInstance,
    this.unique,
    this.model = '',
    this.software = '',
    this.modelVersion = '',
    this.serial = '',
    this.productCode,
    this.n2kVersion,
    this.certification,
    this.len,
    this.install1 = '',
    this.install2 = '',
    this.makerInfo = '',
    this.transmits = const [],
    this.receives = const [],
    this.heartbeatMs,
    this.heartbeatAgeMs,
    this.addressChanges = 0,
    this.previousAddress,
  });

  final int address;
  final bool self;
  final String title;
  final String role;
  final int deviceClass;
  final int function;
  final int ageMs;
  final bool complete;
  final List<NetIssue> issues;
  final String maker;
  final int? manufacturerCode;
  final int? instance;
  final int? systemInstance;
  final int? unique;
  final String model;
  final String software;
  final String modelVersion;
  final String serial;
  final int? productCode;
  final int? n2kVersion;
  final int? certification;

  /// Load equivalency, x 50 mA. Null until product information arrives.
  final int? len;
  final String install1;
  final String install2;
  final String makerInfo;
  final List<int> transmits;
  final List<int> receives;
  final int? heartbeatMs;
  final int? heartbeatAgeMs;
  final int addressChanges;
  final int? previousAddress;

  bool get silent => issues.any((i) => i.code == 'silent');

  static NetDevice fromJson(Map<String, Object?> j) {
    List<int> ints(Object? v) =>
        v is List ? [for (final x in v) _int(x)] : const [];
    return NetDevice(
      address: _int(j['address']),
      self: j['self'] == true,
      title: _str(j['title']),
      role: _str(j['role']),
      deviceClass: _int(j['class']),
      function: _int(j['function']),
      ageMs: _int(j['age_ms']),
      complete: j['complete'] == true,
      issues: [
        if (j['issues'] is List)
          for (final i in j['issues'] as List)
            if (i is Map) NetIssue(_str(i['code']), _str(i['text'])),
      ],
      maker: _str(j['maker']),
      manufacturerCode: j['mfr'] is num ? _int(j['mfr']) : null,
      instance: j['instance'] is num ? _int(j['instance']) : null,
      systemInstance: j['system_instance'] is num
          ? _int(j['system_instance'])
          : null,
      unique: j['unique'] is num ? _int(j['unique']) : null,
      model: _str(j['model']),
      software: _str(j['software']),
      modelVersion: _str(j['model_version']),
      serial: _str(j['serial']),
      productCode: j['product_code'] is num ? _int(j['product_code']) : null,
      n2kVersion: j['n2k_version'] is num ? _int(j['n2k_version']) : null,
      certification: j['certification'] is num
          ? _int(j['certification'])
          : null,
      len: j['len'] is num ? _int(j['len']) : null,
      install1: _str(j['install1']),
      install2: _str(j['install2']),
      makerInfo: _str(j['maker_info']),
      transmits: ints(j['transmits']),
      receives: ints(j['receives']),
      heartbeatMs: j['heartbeat_ms'] is num ? _int(j['heartbeat_ms']) : null,
      heartbeatAgeMs: j['heartbeat_age_ms'] is num
          ? _int(j['heartbeat_age_ms'])
          : null,
      addressChanges: _int(j['address_changes']),
      previousAddress: j['previous_address'] is num
          ? _int(j['previous_address'])
          : null,
    );
  }
}

/// GET /n2k/network.json.
class Network {
  const Network({
    required this.raw,
    required this.devices,
    required this.lenTotal,
    required this.asking,
  });

  final String raw;
  final List<NetDevice> devices;
  final int lenTotal;

  /// Devices the terminal is still asking about.
  final int asking;

  int get problems => devices.where((d) => d.issues.isNotEmpty).length;

  /// The terminal first, then by class and address: the order the terminal
  /// draws them in.
  List<NetDevice> get ordered => [...devices]
    ..sort((a, b) {
      if (a.self != b.self) return a.self ? -1 : 1;
      if (a.deviceClass != b.deviceClass) return a.deviceClass - b.deviceClass;
      return a.address - b.address;
    });

  static Network? parse(String body) {
    Object? d;
    try {
      d = jsonDecode(body);
    } on FormatException {
      return null;
    }
    if (d is! Map || d['v'] != 1 || d['devices'] is! List) return null;
    final discovery = d['discovery'] is Map ? d['discovery'] as Map : const {};
    return Network(
      raw: body,
      devices: [
        for (final e in d['devices'] as List)
          if (e is Map) NetDevice.fromJson(e.cast<String, Object?>()),
      ],
      lenTotal: _int(d['len_total']),
      asking: _int(discovery['asking']),
    );
  }
}

class PgnLine {
  const PgnLine({
    required this.pgn,
    required this.name,
    required this.rate,
    required this.messages,
    required this.ageMs,
    required this.hex,
    required this.text,
  });

  final int pgn;
  final String name;
  final double rate;
  final int messages;
  final int ageMs;
  final String hex;
  final String text;

  static PgnLine fromJson(Map<String, Object?> j) => PgnLine(
    pgn: _int(j['pgn']),
    name: _str(j['name']),
    rate: (j['rate'] as num?)?.toDouble() ?? 0,
    messages: _int(j['msgs']),
    ageMs: _int(j['age_ms']),
    hex: _str(j['hex']),
    text: _str(j['text']),
  );
}

/// GET /n2k/device.json?src=N.
class DeviceDetail {
  const DeviceDetail({
    required this.raw,
    required this.device,
    required this.owns,
    required this.pgns,
    required this.recent,
  });

  final String raw;
  final NetDevice device;

  /// Values the terminal takes from this device ("depth", "position", ...).
  final List<String> owns;
  final List<PgnLine> pgns;
  final List<PgnLine> recent;

  static DeviceDetail? parse(String body) {
    Object? d;
    try {
      d = jsonDecode(body);
    } on FormatException {
      return null;
    }
    if (d is! Map || d['v'] != 1 || d['device'] is! Map) return null;
    List<PgnLine> lines(Object? v) => [
      if (v is List)
        for (final e in v)
          if (e is Map) PgnLine.fromJson(e.cast<String, Object?>()),
    ];
    return DeviceDetail(
      raw: body,
      device: NetDevice.fromJson((d['device'] as Map).cast<String, Object?>()),
      owns: [
        if (d['owns'] is List)
          for (final o in d['owns'] as List) _str(o),
      ],
      pgns: lines(d['pgns']),
      recent: lines(d['recent']),
    );
  }
}

Future<String?> _get(String path, http.Client? client) async {
  final c = client ?? http.Client();
  try {
    final res = await c
        .get(Uri.parse('http://$kTerminalHttpHost$path'))
        .timeout(const Duration(seconds: 3));
    return res.statusCode == 200 ? res.body : null;
  } catch (_) {
    return null;
  } finally {
    if (client == null) c.close();
  }
}

Future<Network?> readNetwork({
  bool refresh = false,
  http.Client? client,
}) async {
  final body = await _get(
    '/n2k/network.json${refresh ? '?refresh=1' : ''}',
    client,
  );
  return body == null ? null : Network.parse(body);
}

Future<DeviceDetail?> readDevice(int address, {http.Client? client}) async {
  final body = await _get('/n2k/device.json?src=$address', client);
  return body == null ? null : DeviceDetail.parse(body);
}

String _age(int ms) {
  if (ms >= 0xFFFFFFFF || ms < 0) return 'never';
  if (ms < 1000) return 'now';
  if (ms < 60000) return '${ms ~/ 1000} s ago';
  return '${ms ~/ 60000} min ago';
}

Color _border(NetDevice d) {
  if (d.self) return const Color(0xFF00B4A6);
  if (d.silent) return const Color(0xFF5C7080);
  return d.issues.isEmpty ? const Color(0xFF1E3A5F) : toneColor(Tone.caution);
}

String _stateLine(NetDevice d) {
  final len = d.len == null ? 'LEN ?' : '${d.len} LEN';
  if (d.silent) return 'Silent · $len';
  if (d.issues.isNotEmpty) {
    return '${d.issues.length} ${d.issues.length == 1 ? 'issue' : 'issues'} · $len';
  }
  return 'OK · $len';
}

// ---------------------------------------------------------------------------

class NetworkPage extends StatelessWidget {
  const NetworkPage({super.key, this.read, this.readDetail});

  final Future<Network?> Function({bool refresh})? read;
  final Future<DeviceDetail?> Function(int address)? readDetail;

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(title: const Text('Network')),
    body: ListView(
      padding: const EdgeInsets.fromLTRB(0, 12, 0, 32),
      children: [NetworkView(read: read, readDetail: readDetail)],
    ),
  );
}

/// The topology: summary, Refresh and Share, the map, what needs a look, and
/// the device list. Polls every 2 s. Sits at the top of the Network tab and
/// on its own page.
class NetworkView extends StatefulWidget {
  const NetworkView({super.key, this.read, this.readDetail});

  final Future<Network?> Function({bool refresh})? read;
  final Future<DeviceDetail?> Function(int address)? readDetail;

  @override
  State<NetworkView> createState() => _NetworkViewState();
}

class _NetworkViewState extends State<NetworkView> {
  Network? _net;
  bool _tried = false;
  Timer? _timer;

  Future<void> _load({bool refresh = false}) async {
    final r = widget.read;
    final n = r != null
        ? await r(refresh: refresh)
        : await readNetwork(refresh: refresh);
    if (!mounted) return;
    setState(() {
      _tried = true;
      if (n != null) _net = n;
    });
  }

  @override
  void initState() {
    super.initState();
    unawaited(_load());
    _timer = Timer.periodic(
      const Duration(seconds: 2),
      (_) => unawaited(_load()),
    );
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  Future<void> _share() async {
    final n = _net;
    if (n == null) return;
    final f = File(
      '${Directory.systemTemp.path}/lmyc-network-${DateTime.now().millisecondsSinceEpoch}.json',
    );
    await f.writeAsString(n.raw);
    await SharePlus.instance.share(
      ShareParams(
        files: [XFile(f.path, mimeType: 'application/json')],
        subject: 'LMYC NMEA 2000 network',
      ),
    );
  }

  void _open(NetDevice d) {
    unawaited(
      Navigator.of(context).push(
        MaterialPageRoute<void>(
          builder: (_) => DeviceDetailPage(
            address: d.address,
            title: d.title,
            read: widget.readDetail,
          ),
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final p = Palette.of(context);
    final n = _net;
    final summary = n == null
        ? (_tried
              ? 'No network from the terminal. Join the boat Wi-Fi on the Boat tab.'
              : 'Reading the terminal…')
        : '${n.devices.length} devices · bus load ${n.lenTotal} LEN '
              '(${(n.lenTotal * 0.05).toStringAsFixed(2)} A)'
              '${n.asking > 0 ? ' · still asking ${n.asking}' : ''}';
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(20, 0, 8, 0),
          child: Row(
            children: [
              Expanded(child: SectionLabel('Network')),
              IconButton(
                key: const ValueKey('network-refresh'),
                tooltip: 'Ask every device again',
                onPressed: () => unawaited(_load(refresh: true)),
                icon: const Icon(Icons.refresh),
              ),
              IconButton(
                key: const ValueKey('network-share'),
                tooltip: 'Share network report',
                onPressed: n == null ? null : _share,
                icon: const Icon(Icons.ios_share),
              ),
            ],
          ),
        ),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 20),
          child: Text(
            summary,
            style: TextStyle(
              color: n != null && n.problems > 0
                  ? toneColor(Tone.caution)
                  : p.body,
              fontSize: 15,
              fontWeight: FontWeight.w600,
            ),
          ),
        ),
        if (n != null) ..._body(context, n),
      ],
    );
  }

  List<Widget> _body(BuildContext context, Network n) {
    final p = Palette.of(context);
    final ordered = n.ordered;
    return [
      Padding(
        padding: const EdgeInsets.fromLTRB(20, 4, 20, 0),
        child: Text(
          'Tap a device for its details. NMEA 2000 has no positions, so devices are in '
          'type order with the installer\'s location text where the device has it.',
          style: TextStyle(color: p.faint, fontSize: 12.5, height: 1.35),
        ),
      ),
      const SizedBox(height: 12),
      TopologyMap(devices: ordered, onTap: _open),
      if (n.problems > 0) ...[
        const SizedBox(height: 12),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 20),
          child: SectionLabel('Needs a look'),
        ),
        for (final d in ordered.where((d) => d.issues.isNotEmpty))
          for (final i in d.issues)
            ListTile(
              dense: true,
              leading: Icon(
                Icons.report_problem_outlined,
                color: toneColor(Tone.caution),
              ),
              title: Text('${d.title} · ${d.address}'),
              subtitle: Text(i.text),
              onTap: () => _open(d),
            ),
      ],
      Theme(
        data: Theme.of(context).copyWith(dividerColor: Colors.transparent),
        child: ExpansionTile(
          key: const ValueKey('network-list'),
          tilePadding: const EdgeInsets.symmetric(horizontal: 20),
          title: Text(
            'All ${n.devices.length} devices as a list',
            style: TextStyle(color: p.body, fontSize: 14),
          ),
          children: [
            for (final d in ordered)
              ListTile(
                key: ValueKey('net-row-${d.address}'),
                title: Text(d.title),
                subtitle: Text(
                  [
                    d.role.isEmpty ? 'Unknown role' : d.role,
                    'address ${d.address}',
                    if (d.install1.isNotEmpty) d.install1,
                  ].join(' · '),
                ),
                trailing: Text(
                  _stateLine(d),
                  style: TextStyle(
                    color: _border(d),
                    fontWeight: FontWeight.w600,
                  ),
                ),
                onTap: () => _open(d),
              ),
          ],
        ),
      ),
    ];
  }
}

/// The backbone as a line with a terminator at each end and every device on a
/// drop, alternately above and below, scrolling sideways when there are many.
class TopologyMap extends StatelessWidget {
  const TopologyMap({super.key, required this.devices, required this.onTap});

  final List<NetDevice> devices;
  final void Function(NetDevice) onTap;

  static const cardW = 150.0;
  static const cardH = 112.0;
  static const step = 84.0;
  static const lineY = cardH + 22;

  @override
  Widget build(BuildContext context) {
    final width =
        24 + (devices.isEmpty ? 0 : (devices.length - 1) * step) + cardW + 24;
    const height = cardH * 2 + 56;
    return SizedBox(
      height: height,
      child: SingleChildScrollView(
        scrollDirection: Axis.horizontal,
        child: SizedBox(
          width: width < 360 ? 360 : width,
          height: height,
          child: Stack(
            children: [
              Positioned(
                left: 8,
                right: 8,
                top: lineY,
                child: Row(
                  children: [
                    _terminator(),
                    Expanded(
                      child: Container(
                        height: 6,
                        color: const Color(0xFF3A5A7C),
                      ),
                    ),
                    _terminator(),
                  ],
                ),
              ),
              for (var i = 0; i < devices.length; i++) ..._node(context, i),
            ],
          ),
        ),
      ),
    );
  }

  Widget _terminator() => Container(
    width: 20,
    height: 20,
    margin: const EdgeInsets.only(top: 0),
    alignment: Alignment.center,
    decoration: BoxDecoration(
      color: const Color(0xFF5C7080),
      borderRadius: BorderRadius.circular(4),
    ),
    child: const Text(
      'T',
      style: TextStyle(
        color: Colors.white,
        fontSize: 11,
        fontWeight: FontWeight.w700,
      ),
    ),
  );

  List<Widget> _node(BuildContext context, int i) {
    final d = devices[i];
    final above = i.isEven;
    final x = 24 + i * step;
    final top = above ? lineY - 18 - cardH : lineY + 24;
    final state = _stateLine(d);
    return [
      Positioned(
        left: x + cardW / 2 - 1.5,
        top: above ? lineY - 18 : lineY + 6,
        child: Container(width: 3, height: 18, color: const Color(0xFF3A5A7C)),
      ),
      Positioned(
        left: x,
        top: top,
        width: cardW,
        height: cardH,
        child: Material(
          color: kNavy,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(12),
            side: BorderSide(color: _border(d), width: 2),
          ),
          child: InkWell(
            key: ValueKey('net-node-${d.address}'),
            borderRadius: BorderRadius.circular(12),
            onTap: () => onTap(d),
            child: Padding(
              padding: const EdgeInsets.all(9),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    d.title,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      color: Colors.white,
                      fontWeight: FontWeight.w700,
                      fontSize: 13.5,
                    ),
                  ),
                  const SizedBox(height: 2),
                  Text(
                    '${d.role.isEmpty ? 'Unknown' : d.role} · ${d.address}',
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      color: Color(0xFF8FB3D9),
                      fontSize: 11.5,
                    ),
                  ),
                  const SizedBox(height: 2),
                  Text(
                    d.install1,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      color: Color(0xFFD5DEE8),
                      fontSize: 11.5,
                    ),
                  ),
                  const Spacer(),
                  Text(
                    state,
                    maxLines: 1,
                    style: TextStyle(
                      color: d.silent
                          ? const Color(0xFF8FA3B8)
                          : d.issues.isEmpty
                          ? const Color(0xFF3DDC97)
                          : toneColor(Tone.caution),
                      fontSize: 12,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    ];
  }
}

// ---------------------------------------------------------------------------

class DeviceDetailPage extends StatefulWidget {
  const DeviceDetailPage({
    super.key,
    required this.address,
    required this.title,
    this.read,
    this.recorder,
  });

  final int address;
  final String title;
  final Future<DeviceDetail?> Function(int address)? read;
  final BusRecorder? recorder;

  @override
  State<DeviceDetailPage> createState() => _DeviceDetailPageState();
}

class _DeviceDetailPageState extends State<DeviceDetailPage> {
  DeviceDetail? _d;
  bool _tried = false;
  bool _showHex = false;
  Timer? _timer;
  late final BusRecorder _rec =
      widget.recorder ?? BusRecorder(src: widget.address);

  Future<void> _load() async {
    final r = widget.read;
    final d = r != null
        ? await r(widget.address)
        : await readDevice(widget.address);
    if (!mounted) return;
    setState(() {
      _tried = true;
      if (d != null) _d = d;
    });
  }

  void _changed() {
    if (mounted) setState(() {});
  }

  @override
  void initState() {
    super.initState();
    _rec.addListener(_changed);
    unawaited(_load());
    _timer = Timer.periodic(
      const Duration(seconds: 1),
      (_) => unawaited(_load()),
    );
  }

  @override
  void dispose() {
    _timer?.cancel();
    _rec.removeListener(_changed);
    if (widget.recorder == null) _rec.dispose();
    super.dispose();
  }

  Future<void> _shareDevice() async {
    final d = _d;
    if (d == null) return;
    final f = File(
      '${Directory.systemTemp.path}/lmyc-device-${widget.address}-${DateTime.now().millisecondsSinceEpoch}.json',
    );
    await f.writeAsString(d.raw);
    await SharePlus.instance.share(
      ShareParams(
        files: [XFile(f.path, mimeType: 'application/json')],
        subject: 'LMYC device ${widget.address}',
      ),
    );
  }

  Future<void> _shareRecording() async {
    final f = _rec.file;
    if (f == null) return;
    await SharePlus.instance.share(
      ShareParams(
        files: [XFile(f.path, mimeType: 'text/plain')],
        subject: 'LMYC capture, device ${widget.address}',
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final d = _d;
    return Scaffold(
      appBar: AppBar(
        title: Text(d?.device.title ?? widget.title),
        actions: [
          IconButton(
            tooltip: 'Share this device',
            onPressed: d == null ? null : _shareDevice,
            icon: const Icon(Icons.ios_share),
          ),
        ],
      ),
      body: d == null
          ? Center(
              child: Text(
                _tried
                    ? 'This device is not on the bus any more.'
                    : 'Reading the terminal…',
              ),
            )
          : _body(context, d),
    );
  }

  Widget _kv(BuildContext context, String k, String v) {
    final p = Palette.of(context);
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 3),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 132,
            child: Text(k, style: TextStyle(color: p.muted, fontSize: 13.5)),
          ),
          Expanded(
            child: Text(v, style: TextStyle(color: p.body, fontSize: 13.5)),
          ),
        ],
      ),
    );
  }

  Widget _body(BuildContext context, DeviceDetail detail) {
    final p = Palette.of(context);
    final d = detail.device;
    final rows = <(String, String)>[
      ('Address', '${d.address}'),
      if (d.role.isNotEmpty) ('Role', d.role),
      if (d.maker.isNotEmpty) ('Maker', d.maker),
      if (d.manufacturerCode != null) ('Maker code', '${d.manufacturerCode}'),
      if (d.instance != null)
        ('Instance', '${d.instance} (system ${d.systemInstance ?? 0})'),
      if (d.unique != null) ('Unique number', '${d.unique}'),
      if (d.model.isNotEmpty) ('Model', d.model),
      if (d.software.isNotEmpty) ('Software', d.software),
      if (d.modelVersion.isNotEmpty) ('Model version', d.modelVersion),
      if (d.serial.isNotEmpty) ('Serial', d.serial),
      if (d.len != null) ('Load', '${d.len} LEN (${d.len! * 50} mA)'),
      if (d.n2kVersion != null && !d.self)
        (
          'NMEA 2000',
          'version ${(d.n2kVersion! / 1000).toStringAsFixed(3)}, certification level ${d.certification}',
        ),
      if (d.install1.isNotEmpty) ('Installed', d.install1),
      if (d.install2.isNotEmpty) ('', d.install2),
      if (d.heartbeatMs != null)
        (
          'Heartbeat',
          'every ${d.heartbeatMs! ~/ 1000} s, last ${_age(d.heartbeatAgeMs ?? 0)}',
        ),
      if (d.addressChanges > 0)
        ('Moved', '${d.addressChanges} times, last from ${d.previousAddress}'),
      if (d.transmits.isNotEmpty) ('Transmit list', d.transmits.join(', ')),
      (
        'Terminal uses it for',
        detail.owns.isEmpty
            ? 'nothing'
            : detail.owns.join(', ').replaceAll('_', ' '),
      ),
      if (!d.self) ('Last heard', _age(d.ageMs)),
    ];
    final r = _rec;
    return ListView(
      padding: const EdgeInsets.fromLTRB(20, 12, 20, 32),
      children: [
        for (final i in d.issues)
          Padding(
            padding: const EdgeInsets.only(bottom: 8),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Icon(
                  Icons.report_problem_outlined,
                  color: toneColor(Tone.caution),
                  size: 20,
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    i.text,
                    style: TextStyle(
                      color: toneColor(Tone.caution),
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ),
              ],
            ),
          ),
        SectionLabel('Identity'),
        const SizedBox(height: 6),
        for (final (k, v) in rows) _kv(context, k, v),
        const SizedBox(height: 18),
        Row(
          children: [
            Expanded(child: SectionLabel('What it sends')),
            TextButton(
              key: const ValueKey('toggle-hex'),
              onPressed: () => setState(() => _showHex = !_showHex),
              child: Text(_showHex ? 'Decoded' : 'Raw bytes'),
            ),
          ],
        ),
        if (detail.pgns.isEmpty)
          Text(
            d.self
                ? 'This terminal sends network management only.'
                : 'Nothing heard from it yet.',
            style: TextStyle(color: p.faint),
          ),
        for (final g in detail.pgns)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 5),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  '${g.pgn} ${g.name} · ${g.rate.toStringAsFixed(1)}/s · ${g.messages} msgs · last ${_age(g.ageMs)}',
                  style: TextStyle(
                    color: p.ink,
                    fontWeight: FontWeight.w600,
                    fontSize: 13.5,
                  ),
                ),
                Text(
                  _showHex || g.text.isEmpty
                      ? (g.hex.isEmpty ? '—' : g.hex)
                      : g.text,
                  style: TextStyle(
                    color: p.muted,
                    fontSize: 13,
                    fontFeatures: const [FontFeature.tabularFigures()],
                  ),
                ),
              ],
            ),
          ),
        const SizedBox(height: 18),
        SectionLabel('Recent messages'),
        const SizedBox(height: 4),
        for (final m in detail.recent.take(20))
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 2),
            child: Text(
              '${(m.ageMs / 1000).toStringAsFixed(1).padLeft(5)} s  ${m.pgn}  '
              '${_showHex || m.text.isEmpty ? m.hex : m.text}',
              style: TextStyle(
                color: p.body,
                fontSize: 12.5,
                fontFeatures: const [FontFeature.tabularFigures()],
              ),
            ),
          ),
        const SizedBox(height: 18),
        SectionLabel('Record this device'),
        const SizedBox(height: 6),
        Text(
          r.running
              ? 'Recording ${formatCount(r.frames)} frames from address ${widget.address}'
              : r.file == null
              ? 'Every frame from this device only, in candump format.'
              : 'Saved ${formatCount(r.frames)} frames',
          style: TextStyle(color: p.body),
        ),
        const SizedBox(height: 8),
        Wrap(
          spacing: 10,
          children: [
            FilledButton.icon(
              key: const ValueKey('record-device'),
              onPressed: () => unawaited(r.running ? r.stop() : r.start()),
              icon: Icon(r.running ? Icons.stop : Icons.fiber_manual_record),
              label: Text(r.running ? 'Stop' : 'Record'),
            ),
            if (!r.running && r.file != null)
              OutlinedButton.icon(
                onPressed: _shareRecording,
                icon: const Icon(Icons.ios_share),
                label: const Text('Share recording'),
              ),
          ],
        ),
      ],
    );
  }
}
