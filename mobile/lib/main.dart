import 'dart:async';
import 'dart:convert';

import 'package:app_links/app_links.dart';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const LmycApp());
}

class LmycApp extends StatelessWidget {
  const LmycApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'LMYC',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF001F3F),
          brightness: Brightness.light,
        ),
        useMaterial3: true,
        appBarTheme: const AppBarTheme(
          centerTitle: true,
          backgroundColor: Color(0xFF001F3F),
          foregroundColor: Colors.white,
        ),
      ),
      home: const HomePage(),
    );
  }
}

enum BlePhase {
  idle,
  checkingPermissions,
  scanning,
  connecting,
  confirming,
  paired,
  failed,
}

class _NearbyHit {
  _NearbyHit(this.result);

  ScanResult result;

  String get id => result.device.remoteId.toString();

  String get name {
    final n = result.advertisementData.advName;
    if (n.isNotEmpty) return n;
    if (result.device.platformName.isNotEmpty) return result.device.platformName;
    return '(no name)';
  }

  int get rssi => result.rssi;

  List<String> get uuids =>
      result.advertisementData.serviceUuids.map((u) => u.toString()).toList();
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final _appLinks = AppLinks();
  final _pasteController = TextEditingController();
  StreamSubscription<Uri>? _linkSubscription;
  StreamSubscription<List<ScanResult>>? _scanSubscription;
  StreamSubscription<BluetoothConnectionState>? _connSubscription;

  String? _boatId;
  String? _terminalId;
  String? _bleName;
  String? _oob;
  String? _version;

  BlePhase _blePhase = BlePhase.idle;
  String _bleDetail = '';
  BluetoothDevice? _device;
  int? _rssi;
  String? _sessionReply;

  int _heardCount = 0;
  final Map<String, _NearbyHit> _nearby = {};

  static final Guid kLmycService =
      Guid('6c6d7963-0001-4000-8000-000000000001');
  static final Guid kLmycPayloadChr =
      Guid('6c6d7963-0001-4000-8000-000000000010');
  static final Guid kLmycSessionChr =
      Guid('6c6d7963-0001-4000-8000-000000000020');

  @override
  void initState() {
    super.initState();
    _initDeepLinks();
  }

  Future<void> _initDeepLinks() async {
    try {
      final initialUri = await _appLinks.getInitialLink();
      if (initialUri != null) _handleIncomingLink(initialUri);
    } catch (e) {
      debugPrint('Failed to get initial link: $e');
    }

    _linkSubscription = _appLinks.uriLinkStream.listen(
      _handleIncomingLink,
      onError: (err) => debugPrint('Deep link error: $err'),
    );
  }

  void _handleIncomingLink(Uri uri) {
    if (uri.scheme != 'lmyc') return;
    final params = uri.queryParameters;
    setState(() {
      _version = params['v'];
      _boatId = params['boat'];
      _terminalId = params['tid'];
      _bleName = params['ble'];
      _oob = params['oob'];
    });
  }

  void _applyPastedUri() {
    final raw = _pasteController.text.trim();
    if (raw.isEmpty) return;
    final uri = Uri.tryParse(raw);
    if (uri == null || uri.scheme != 'lmyc') {
      setState(() {
        _blePhase = BlePhase.failed;
        _bleDetail = 'Not an lmyc:// URI.';
      });
      return;
    }
    _handleIncomingLink(uri);
  }

  Future<bool> _ensurePermissions() async {
    setState(() {
      _blePhase = BlePhase.checkingPermissions;
      _bleDetail = 'Requesting Bluetooth permissions…';
    });

    final statuses = await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.locationWhenInUse,
    ].request();

    final scanOk = statuses[Permission.bluetoothScan]?.isGranted ?? true;
    final connectOk = statuses[Permission.bluetoothConnect]?.isGranted ?? true;

    if (!scanOk || !connectOk) {
      setState(() {
        _blePhase = BlePhase.failed;
        _bleDetail = 'Bluetooth permission denied.';
      });
      return false;
    }

    final adapterState = await FlutterBluePlus.adapterState.first;
    if (adapterState != BluetoothAdapterState.on) {
      setState(() {
        _blePhase = BlePhase.failed;
        _bleDetail = 'Turn on Bluetooth and try again.';
      });
      return false;
    }
    return true;
  }

  bool _isLmycHit(ScanResult r) {
    final names = <String>[
      r.device.platformName,
      r.advertisementData.advName,
    ].where((s) => s.isNotEmpty);

    final uuidMatch = r.advertisementData.serviceUuids.any(
      (u) => u == kLmycService,
    );
    final prefixMatch = names.any((n) => n.toUpperCase().startsWith('LMYC-'));
    final exactMatch =
        _bleName != null && names.any((n) => n == _bleName);
    return exactMatch || prefixMatch || uuidMatch;
  }

  Future<void> _startScan({required bool autoConnectExact}) async {
    await _disconnect(resetPhase: false);

    if (!await _ensurePermissions()) return;

    setState(() {
      _blePhase = BlePhase.scanning;
      _bleDetail = autoConnectExact && _bleName != null
          ? 'Scanning for $_bleName …'
          : 'Scanning for LMYC-* terminals …';
      _rssi = null;
      _device = null;
      _sessionReply = null;
      _heardCount = 0;
      _nearby.clear();
    });

    try {
      await FlutterBluePlus.stopScan();
      await _scanSubscription?.cancel();

      final completer = Completer<ScanResult?>();

      _scanSubscription = FlutterBluePlus.scanResults.listen((results) {
        _heardCount = results.length;
        for (final r in results) {
          if (!_isLmycHit(r)) continue;
          final hit = _NearbyHit(r);
          _nearby[hit.id] = hit;
          if (autoConnectExact &&
              _bleName != null &&
              hit.name == _bleName &&
              !completer.isCompleted) {
            completer.complete(r);
          }
        }
        if (mounted) setState(() {});
      });

      // Unfiltered scan: LMYC UUID lives in scan-response and Android
      // withServices filters often miss that. We match name/prefix in-app.
      await FlutterBluePlus.startScan(
        timeout: const Duration(seconds: 15),
        androidUsesFineLocation: false,
        androidScanMode: AndroidScanMode.lowLatency,
      );

      ScanResult? found;
      if (autoConnectExact && _bleName != null) {
        found = await completer.future.timeout(
          const Duration(seconds: 15),
          onTimeout: () => null,
        );
      } else {
        await FlutterBluePlus.isScanning
            .firstWhere((v) => v == false)
            .timeout(const Duration(seconds: 16), onTimeout: () => false);
      }

      await FlutterBluePlus.stopScan();
      await _scanSubscription?.cancel();
      _scanSubscription = null;

      if (!mounted) return;

      if (autoConnectExact) {
        found ??= _nearby.values
            .where((h) => h.name == _bleName)
            .map((h) => h.result)
            .firstOrNull;
        if (found == null) {
          setState(() {
            _blePhase = BlePhase.failed;
            _bleDetail = _nearby.isEmpty
                ? 'Heard $_heardCount BLE device(s), 0 named LMYC-*.\n'
                    'Terminal BLE line should read Advertising.\n'
                    'Android Settings often hides LE-only devices — use this screen.'
                : 'Did not find "$_bleName". Nearby: '
                    '${_nearby.values.map((h) => h.name).join(", ")}';
          });
          return;
        }
        setState(() {
          _device = found!.device;
          _rssi = found.rssi;
          _blePhase = BlePhase.connecting;
          _bleDetail = 'Found ${found.advertisementData.advName.isNotEmpty ? found.advertisementData.advName : _bleName} '
              '(${found.rssi} dBm) — connecting…';
        });
        await _connectAndConfirm(found.device);
        return;
      }

      setState(() {
        _blePhase = _nearby.isEmpty ? BlePhase.failed : BlePhase.idle;
        _bleDetail = _nearby.isEmpty
            ? 'Heard $_heardCount BLE device(s), 0 LMYC hits.\n'
                'Confirm the terminal DIAGNOSTICS card says Advertising.'
            : 'Found ${_nearby.length} LMYC terminal(s). Tap one to connect.';
      });
    } catch (e) {
      setState(() {
        _blePhase = BlePhase.failed;
        _bleDetail = 'Scan error: $e';
      });
    }
  }

  Future<void> _ensureConnected(BluetoothDevice device) async {
    if (device.isConnected) return;
    await device.connect(
      timeout: const Duration(seconds: 20),
      autoConnect: false,
      mtu: null, // never auto-request MTU — drops this ESP32
    );
    await device.connectionState
        .firstWhere((s) => s == BluetoothConnectionState.connected)
        .timeout(const Duration(seconds: 10));
  }

  Future<List<BluetoothService>> _discoverWithRetry(BluetoothDevice device) async {
    await Future<void>.delayed(const Duration(milliseconds: 1200));

    for (var attempt = 1; attempt <= 3; attempt++) {
      await _ensureConnected(device);

      setState(() {
        _bleDetail = 'Discovering services (attempt $attempt/3)…';
      });

      try {
        final services = await device.discoverServices();
        if (services.isNotEmpty) {
          debugPrint('discoverServices ok: ${services.length} services');
          return services;
        }
        debugPrint('discoverServices returned 0 services (attempt $attempt)');
      } catch (e) {
        debugPrint('discoverServices attempt $attempt failed: $e');
      }

      await Future<void>.delayed(Duration(milliseconds: 800 * attempt));
    }

    throw Exception(
      'GATT discovery returned no services after 3 attempts. '
      'Terminal may still show Connected — try again.',
    );
  }

  Future<void> _connectAndConfirm(BluetoothDevice device) async {
    try {
      await _connSubscription?.cancel();
      _connSubscription = device.connectionState.listen((state) {
        debugPrint('connectionState: $state');
        if (!mounted) return;
        if (state == BluetoothConnectionState.disconnected &&
            (_blePhase == BlePhase.paired || _blePhase == BlePhase.confirming)) {
          setState(() {
            _blePhase = BlePhase.idle;
            _bleDetail = 'Disconnected from terminal';
            _sessionReply = null;
          });
        }
      });

      try {
        await device.removeBond();
      } catch (_) {}

      await _ensureConnected(device);

      setState(() {
        _blePhase = BlePhase.confirming;
        _bleDetail = 'Connected — discovering services…';
      });

      final services = await _discoverWithRetry(device);

      setState(() {
        _bleDetail = _oob == null
            ? 'Connected. Scan/paste QR to prove OOB and finish pairing.'
            : 'Connected — proving OOB from QR…';
      });

      BluetoothCharacteristic? sessionChr;
      BluetoothCharacteristic? payloadChr;

      for (final s in services) {
        debugPrint('service: ${s.uuid}');
        if (s.uuid == kLmycService) {
          for (final c in s.characteristics) {
            debugPrint('  char: ${c.uuid}');
            if (c.uuid == kLmycSessionChr) sessionChr = c;
            if (c.uuid == kLmycPayloadChr) payloadChr = c;
          }
        }
      }

      if (payloadChr != null) {
        try {
          final payload = await payloadChr.read();
          debugPrint('payload bytes: ${payload.length}');
        } catch (e) {
          debugPrint('payload read: $e');
        }
      }

      if (sessionChr == null) {
        setState(() {
          _blePhase = BlePhase.failed;
          _bleDetail =
              'Connected, but LMYC session characteristic not found.\n'
              'Services seen: ${services.map((s) => s.uuid).join(", ")}';
        });
        return;
      }

      if (_oob == null) {
        setState(() {
          _blePhase = BlePhase.idle;
          _sessionReply = 'WAIT (no OOB yet)';
          _bleDetail =
              'Link is up. Scan the terminal QR (or paste lmyc://) then tap Pair.';
        });
        return;
      }

      final cmd = 'PAIR $_oob';
      await sessionChr.write(utf8.encode(cmd), withoutResponse: false);

      await Future<void>.delayed(const Duration(milliseconds: 300));
      final replyBytes = await sessionChr.read();
      final reply = utf8.decode(replyBytes).trim();

      if (!mounted) return;

      if (reply == 'OK') {
        setState(() {
          _blePhase = BlePhase.paired;
          _sessionReply = reply;
          _bleDetail =
              'Paired with ${_boatId ?? "boat"}\nTerminal confirmed OOB.';
        });
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Paired with ${_boatId ?? "terminal"}'),
            backgroundColor: const Color(0xFF00B4A6),
          ),
        );
      } else {
        setState(() {
          _blePhase = BlePhase.failed;
          _sessionReply = reply;
          _bleDetail =
              'Terminal rejected OOB (reply: $reply).\n'
              'Rescan the QR — OOB rotates on terminal reboot.';
        });
      }
    } catch (e) {
      setState(() {
        _blePhase = BlePhase.failed;
        _bleDetail = 'Pairing failed: $e';
      });
    }
  }

  Future<void> _disconnect({bool resetPhase = true}) async {
    await FlutterBluePlus.stopScan();
    await _scanSubscription?.cancel();
    _scanSubscription = null;
    await _connSubscription?.cancel();
    _connSubscription = null;
    try {
      await _device?.disconnect();
    } catch (_) {}
    if (resetPhase && mounted) {
      setState(() {
        _blePhase = BlePhase.idle;
        _bleDetail = '';
        _sessionReply = null;
      });
    }
  }

  @override
  void dispose() {
    _linkSubscription?.cancel();
    _pasteController.dispose();
    _disconnect(resetPhase: false);
    super.dispose();
  }

  String _short(String? value, [int max = 20]) {
    if (value == null || value.isEmpty) return '—';
    if (value.length <= max) return value;
    return '${value.substring(0, max)}…';
  }

  Color get _phaseColor {
    switch (_blePhase) {
      case BlePhase.paired:
        return const Color(0xFF00B4A6);
      case BlePhase.failed:
        return Colors.red.shade700;
      case BlePhase.scanning:
      case BlePhase.connecting:
      case BlePhase.confirming:
      case BlePhase.checkingPermissions:
        return Colors.orange.shade700;
      case BlePhase.idle:
        return Colors.black54;
    }
  }

  String get _phaseLabel {
    switch (_blePhase) {
      case BlePhase.idle:
        return 'Idle';
      case BlePhase.checkingPermissions:
        return 'Permissions';
      case BlePhase.scanning:
        return 'Scanning';
      case BlePhase.connecting:
        return 'Connecting';
      case BlePhase.confirming:
        return 'Confirming';
      case BlePhase.paired:
        return 'Paired';
      case BlePhase.failed:
        return 'Failed';
    }
  }

  @override
  Widget build(BuildContext context) {
    final hasPairingData = _oob != null && _bleName != null;
    final busy = _blePhase == BlePhase.scanning ||
        _blePhase == BlePhase.connecting ||
        _blePhase == BlePhase.confirming ||
        _blePhase == BlePhase.checkingPermissions;
    final isPaired = _blePhase == BlePhase.paired;
    final hits = _nearby.values.toList()
      ..sort((a, b) => b.rssi.compareTo(a.rssi));

    return Scaffold(
      appBar: AppBar(
        title: const Text('LMYC Boat Terminal'),
      ),
      body: SafeArea(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(24),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const SizedBox(height: 8),
              Icon(
                isPaired ? Icons.verified : Icons.sailing,
                size: 64,
                color: isPaired
                    ? const Color(0xFF00B4A6)
                    : const Color(0xFF001F3F),
              ),
              const SizedBox(height: 12),
              Text(
                isPaired ? 'Paired' : 'Lower Mainland Yacht Club',
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                      fontWeight: FontWeight.bold,
                      color: const Color(0xFF001F3F),
                    ),
              ),
              const SizedBox(height: 4),
              Text(
                isPaired
                    ? 'You are connected to ${_boatId ?? "the terminal"}'
                    : 'Companion app for boat check-in and terminal pairing',
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                      color: Colors.black54,
                    ),
              ),
              const SizedBox(height: 28),

              Card(
                elevation: 2,
                child: Padding(
                  padding: const EdgeInsets.all(20),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          Text(
                            'From QR code',
                            style: Theme.of(context)
                                .textTheme
                                .titleMedium
                                ?.copyWith(fontWeight: FontWeight.w600),
                          ),
                          const Spacer(),
                          if (hasPairingData)
                            _badge('Ready', const Color(0xFF00B4A6)),
                        ],
                      ),
                      const SizedBox(height: 14),
                      _statusRow('Boat', _boatId),
                      _statusRow('Terminal', _terminalId),
                      _statusRow('BLE name', _bleName),
                      _statusRow('OOB data', _short(_oob, 18)),
                      _statusRow('Version', _version),
                      const SizedBox(height: 8),
                      TextField(
                        controller: _pasteController,
                        decoration: const InputDecoration(
                          labelText: 'Paste lmyc:// URI if camera does not open the app',
                          border: OutlineInputBorder(),
                          isDense: true,
                        ),
                        minLines: 1,
                        maxLines: 3,
                        onSubmitted: (_) => _applyPastedUri(),
                      ),
                      const SizedBox(height: 8),
                      Align(
                        alignment: Alignment.centerRight,
                        child: TextButton(
                          onPressed: _applyPastedUri,
                          child: const Text('Apply URI'),
                        ),
                      ),
                    ],
                  ),
                ),
              ),

              const SizedBox(height: 16),

              Card(
                elevation: 2,
                child: Padding(
                  padding: const EdgeInsets.all(20),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          Text(
                            'Bluetooth',
                            style: Theme.of(context)
                                .textTheme
                                .titleMedium
                                ?.copyWith(fontWeight: FontWeight.w600),
                          ),
                          const Spacer(),
                          _badge(_phaseLabel, _phaseColor),
                        ],
                      ),
                      const SizedBox(height: 12),
                      if (_bleDetail.isNotEmpty)
                        Text(
                          _bleDetail,
                          style: TextStyle(color: _phaseColor, height: 1.35),
                        ),
                      const SizedBox(height: 8),
                      Text(
                        'Heard $_heardCount BLE device(s) · ${_nearby.length} LMYC hit(s)',
                        style: const TextStyle(color: Colors.black54),
                      ),
                      if (_rssi != null) ...[
                        const SizedBox(height: 8),
                        _statusRow('RSSI', '$_rssi dBm'),
                      ],
                      if (_sessionReply != null) ...[
                        const SizedBox(height: 4),
                        _statusRow('Session', _sessionReply),
                      ],
                      if (hits.isNotEmpty) ...[
                        const SizedBox(height: 12),
                        const Text(
                          'Nearby terminals',
                          style: TextStyle(fontWeight: FontWeight.w600),
                        ),
                        const SizedBox(height: 6),
                        ...hits.map((h) {
                          final match = _bleName != null && h.name == _bleName;
                          return ListTile(
                            dense: true,
                            contentPadding: EdgeInsets.zero,
                            leading: Icon(
                              Icons.bluetooth,
                              color: match
                                  ? const Color(0xFF00B4A6)
                                  : const Color(0xFF001F3F),
                            ),
                            title: Text(h.name),
                            subtitle: Text(
                              '${h.rssi} dBm'
                              '${h.uuids.isEmpty ? '' : ' · ${h.uuids.length} uuid'}',
                            ),
                            trailing: TextButton(
                              onPressed: busy
                                  ? null
                                  : () {
                                      setState(() {
                                        _device = h.result.device;
                                        _rssi = h.rssi;
                                        _blePhase = BlePhase.connecting;
                                        _bleDetail =
                                            'Connecting to ${h.name}…';
                                      });
                                      _connectAndConfirm(h.result.device);
                                    },
                              child: const Text('Connect'),
                            ),
                          );
                        }),
                      ],
                      const SizedBox(height: 16),
                      SizedBox(
                        width: double.infinity,
                        child: OutlinedButton.icon(
                          onPressed: busy
                              ? null
                              : () => _startScan(autoConnectExact: false),
                          icon: const Icon(Icons.radar),
                          label: Text(busy ? 'Working…' : 'Find terminals'),
                          style: OutlinedButton.styleFrom(
                            padding: const EdgeInsets.symmetric(vertical: 14),
                          ),
                        ),
                      ),
                      const SizedBox(height: 8),
                      SizedBox(
                        width: double.infinity,
                        child: FilledButton.icon(
                          onPressed: busy
                              ? null
                              : isPaired
                                  ? () => _disconnect()
                                  : hasPairingData
                                      ? () => _startScan(autoConnectExact: true)
                                      : null,
                          icon: Icon(
                            isPaired
                                ? Icons.link_off
                                : Icons.bluetooth_searching,
                          ),
                          label: Text(
                            isPaired
                                ? 'Disconnect'
                                : busy
                                    ? 'Working…'
                                    : 'Connect & pair',
                          ),
                          style: FilledButton.styleFrom(
                            backgroundColor: const Color(0xFF001F3F),
                            padding: const EdgeInsets.symmetric(vertical: 14),
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ),

              if (isPaired) ...[
                const SizedBox(height: 16),
                Card(
                  color: const Color(0xFF00B4A6).withValues(alpha: 0.08),
                  elevation: 0,
                  child: const Padding(
                    padding: EdgeInsets.all(20),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          'Bench pairing complete',
                          style: TextStyle(
                            fontWeight: FontWeight.w600,
                            fontSize: 16,
                          ),
                        ),
                        SizedBox(height: 8),
                        Text(
                          'The phone proved it scanned this terminal’s QR '
                          '(OOB match). Next: booking token, LE Secure '
                          'Connections, and member tools.',
                          style: TextStyle(height: 1.4, color: Colors.black87),
                        ),
                      ],
                    ),
                  ),
                ),
              ],

              const SizedBox(height: 24),
              Text(
                'Find terminals works without a QR. Pairing still needs the QR OOB.\n'
                'Watch the terminal DIAGNOSTICS card: Advertising → Connected → Paired.',
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      color: Colors.black45,
                    ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _badge(String text, Color color) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.15),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Text(
        text,
        style: TextStyle(
          color: color,
          fontWeight: FontWeight.w600,
          fontSize: 12,
        ),
      ),
    );
  }

  Widget _statusRow(String label, String? value) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 100,
            child: Text(
              label,
              style: const TextStyle(
                fontWeight: FontWeight.w500,
                color: Colors.black54,
              ),
            ),
          ),
          Expanded(
            child: Text(
              value ?? '—',
              style: const TextStyle(fontFamily: 'monospace'),
            ),
          ),
        ],
      ),
    );
  }
}
