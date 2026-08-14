import 'dart:async';

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
  connected,
  failed,
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final _appLinks = AppLinks();
  StreamSubscription<Uri>? _linkSubscription;
  StreamSubscription<List<ScanResult>>? _scanSubscription;
  StreamSubscription<BluetoothConnectionState>? _connSubscription;

  // From QR / deep link
  String? _lastDeepLink;
  String? _boatId;
  String? _terminalId;
  String? _bleName;
  String? _oob;
  String? _version;

  // BLE state
  BlePhase _blePhase = BlePhase.idle;
  String _bleDetail = '';
  BluetoothDevice? _device;
  int? _rssi;

  // LMYC GATT (must match firmware)
  static final Guid kLmycService =
      Guid('6c6d7963-0001-4000-8000-000000000001');
  static final Guid kLmycPayloadChr =
      Guid('6c6d7963-0001-4000-8000-000000000010');

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
    debugPrint('Incoming deep link: $uri');
    if (uri.scheme != 'lmyc') return;

    final params = uri.queryParameters;
    setState(() {
      _lastDeepLink = uri.toString();
      _version = params['v'];
      _boatId = params['boat'];
      _terminalId = params['tid'];
      _bleName = params['ble'];
      _oob = params['oob'];
    });

    if (_oob != null && mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Pairing data for ${_boatId ?? "boat"} — ready to connect'),
          backgroundColor: const Color(0xFF00B4A6),
        ),
      );
    }
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
        _bleDetail =
            'Bluetooth permission denied. Enable it in system settings.';
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

  Future<void> _startScanAndConnect() async {
    if (_bleName == null || _bleName!.isEmpty) {
      setState(() {
        _blePhase = BlePhase.failed;
        _bleDetail = 'No BLE name yet — scan the terminal QR first.';
      });
      return;
    }

    await _disconnect();

    if (!await _ensurePermissions()) return;

    setState(() {
      _blePhase = BlePhase.scanning;
      _bleDetail = 'Scanning for $_bleName …';
      _rssi = null;
      _device = null;
    });

    try {
      await FlutterBluePlus.stopScan();
      await _scanSubscription?.cancel();

      final completer = Completer<ScanResult?>();
      Timer? timeout;

      _scanSubscription = FlutterBluePlus.scanResults.listen((results) {
        for (final r in results) {
          final name = r.device.platformName;
          final advName = r.advertisementData.advName;
          final match = name == _bleName || advName == _bleName;
          if (match && !completer.isCompleted) {
            completer.complete(r);
          }
        }
      });

      await FlutterBluePlus.startScan(
        timeout: const Duration(seconds: 12),
        androidUsesFineLocation: false,
      );

      timeout = Timer(const Duration(seconds: 12), () {
        if (!completer.isCompleted) completer.complete(null);
      });

      final found = await completer.future;
      timeout.cancel();
      await FlutterBluePlus.stopScan();
      await _scanSubscription?.cancel();
      _scanSubscription = null;

      if (found == null) {
        setState(() {
          _blePhase = BlePhase.failed;
          _bleDetail =
              'Did not find "$_bleName".\nIs the terminal powered and showing the QR?';
        });
        return;
      }

      setState(() {
        _device = found.device;
        _rssi = found.rssi;
        _blePhase = BlePhase.connecting;
        _bleDetail = 'Found $_bleName (${found.rssi} dBm) — connecting…';
      });

      await _connectTo(found.device);
    } catch (e) {
      setState(() {
        _blePhase = BlePhase.failed;
        _bleDetail = 'Scan error: $e';
      });
    }
  }

  Future<void> _connectTo(BluetoothDevice device) async {
    try {
      await _connSubscription?.cancel();
      _connSubscription = device.connectionState.listen((state) {
        if (!mounted) return;
        if (state == BluetoothConnectionState.disconnected &&
            _blePhase == BlePhase.connected) {
          setState(() {
            _blePhase = BlePhase.idle;
            _bleDetail = 'Disconnected';
          });
        }
      });

      await device.connect(
        timeout: const Duration(seconds: 15),
        autoConnect: false,
      );

      // Discover services and try to read the payload characteristic
      String extra = '';
      try {
        final services = await device.discoverServices();
        for (final s in services) {
          if (s.uuid == kLmycService) {
            for (final c in s.characteristics) {
              if (c.uuid == kLmycPayloadChr) {
                final value = await c.read();
                final text = String.fromCharCodes(value);
                extra = '\nPayload OK (${text.length} chars)';
                break;
              }
            }
          }
        }
      } catch (e) {
        extra = '\n(Connected; characteristic read skipped: $e)';
      }

      if (!mounted) return;
      setState(() {
        _blePhase = BlePhase.connected;
        _bleDetail = 'Connected to ${device.platformName}$extra';
      });

      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Connected to boat terminal'),
          backgroundColor: Color(0xFF00B4A6),
        ),
      );
    } catch (e) {
      setState(() {
        _blePhase = BlePhase.failed;
        _bleDetail = 'Connect failed: $e';
      });
    }
  }

  Future<void> _disconnect() async {
    await FlutterBluePlus.stopScan();
    await _scanSubscription?.cancel();
    _scanSubscription = null;
    await _connSubscription?.cancel();
    _connSubscription = null;
    try {
      await _device?.disconnect();
    } catch (_) {}
  }

  @override
  void dispose() {
    _linkSubscription?.cancel();
    _disconnect();
    super.dispose();
  }

  String _short(String? value, [int max = 20]) {
    if (value == null || value.isEmpty) return '—';
    if (value.length <= max) return value;
    return '${value.substring(0, max)}…';
  }

  Color get _phaseColor {
    switch (_blePhase) {
      case BlePhase.connected:
        return const Color(0xFF00B4A6);
      case BlePhase.failed:
        return Colors.red.shade700;
      case BlePhase.scanning:
      case BlePhase.connecting:
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
      case BlePhase.connected:
        return 'Connected';
      case BlePhase.failed:
        return 'Failed';
    }
  }

  @override
  Widget build(BuildContext context) {
    final hasPairingData = _oob != null && _bleName != null;
    final busy = _blePhase == BlePhase.scanning ||
        _blePhase == BlePhase.connecting ||
        _blePhase == BlePhase.checkingPermissions;

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
              const Icon(
                Icons.sailing,
                size: 64,
                color: Color(0xFF001F3F),
              ),
              const SizedBox(height: 12),
              Text(
                'Lower Mainland Yacht Club',
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                      fontWeight: FontWeight.bold,
                      color: const Color(0xFF001F3F),
                    ),
              ),
              const SizedBox(height: 4),
              Text(
                'Companion app for boat check-in and terminal pairing',
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                      color: Colors.black54,
                    ),
              ),
              const SizedBox(height: 28),

              // Deep-link card
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
                    ],
                  ),
                ),
              ),

              const SizedBox(height: 16),

              // BLE card
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
                          style: TextStyle(
                            color: _phaseColor,
                            height: 1.35,
                          ),
                        ),
                      if (_rssi != null) ...[
                        const SizedBox(height: 8),
                        _statusRow('RSSI', '$_rssi dBm'),
                      ],
                      const SizedBox(height: 16),
                      SizedBox(
                        width: double.infinity,
                        child: FilledButton.icon(
                          onPressed: busy
                              ? null
                              : hasPairingData
                                  ? (_blePhase == BlePhase.connected
                                      ? () async {
                                          await _disconnect();
                                          setState(() {
                                            _blePhase = BlePhase.idle;
                                            _bleDetail = 'Disconnected';
                                          });
                                        }
                                      : _startScanAndConnect)
                                  : null,
                          icon: Icon(
                            _blePhase == BlePhase.connected
                                ? Icons.link_off
                                : Icons.bluetooth_searching,
                          ),
                          label: Text(
                            _blePhase == BlePhase.connected
                                ? 'Disconnect'
                                : busy
                                    ? 'Working…'
                                    : 'Connect to terminal',
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

              const SizedBox(height: 24),
              Text(
                hasPairingData
                    ? 'System Bluetooth Settings often hides LE-only devices.\nUse the button above — in-app scan is more reliable.'
                    : 'Scan the QR code on the boat terminal to load pairing data.',
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      color: Colors.black45,
                    ),
              ),
              const SizedBox(height: 8),
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
