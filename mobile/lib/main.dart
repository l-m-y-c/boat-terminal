import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:app_links/app_links.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

import 'boat_wifi.dart';
import 'boatlink.dart';
import 'home_flow.dart';
import 'last_boat.dart';
import 'maintenance_book.dart';
import 'nmea_console.dart';
import 'pairing_link.dart';
import 'protocol.dart';
import 'qr_scan_page.dart';

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
    if (result.device.platformName.isNotEmpty) {
      return result.device.platformName;
    }
    return '(no name)';
  }

  int get rssi => result.rssi;

  List<String> get uuids =>
      result.advertisementData.serviceUuids.map((u) => u.toString()).toList();
}

class _LinkStatusRow extends StatelessWidget {
  const _LinkStatusRow({required this.row});

  final LinkRow row;

  @override
  Widget build(BuildContext context) {
    final color = switch (row.mark) {
      LinkMark.done => const Color(0xFF1E4D3A),
      LinkMark.wait => const Color(0xFF8A6A12),
      LinkMark.fail => const Color(0xFF8C2F2F),
      LinkMark.idle => const Color(0xFF5C6B7A),
    };
    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Container(
          width: 10,
          height: 10,
          margin: const EdgeInsets.only(top: 5, right: 10),
          decoration: BoxDecoration(color: color, shape: BoxShape.circle),
        ),
        SizedBox(
          width: 108,
          child: Text(
            row.name,
            style: const TextStyle(fontWeight: FontWeight.w600),
          ),
        ),
        Expanded(child: Text(row.value)),
      ],
    );
  }
}

/// What the landing page shows before a terminal is chosen.
enum _Landing { start, searching, countdown, choose, none }

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _UiTick extends ChangeNotifier {
  void ping() => notifyListeners();
}

class _HomePageState extends State<HomePage> with WidgetsBindingObserver {
  final _appLinks = AppLinks();
  final _pasteController = TextEditingController();
  final _ui = _UiTick();
  bool _connectQueued = false;
  StreamSubscription<Uri>? _linkSubscription;
  StreamSubscription<List<ScanResult>>? _scanSubscription;
  StreamSubscription<BluetoothConnectionState>? _connSubscription;

  String? _boatId;
  String? _terminalId;
  String? _bleName;
  String? _oob;

  BlePhase _blePhase = BlePhase.idle;
  String _bleDetail = '';
  BluetoothDevice? _device;
  int? _rssi;
  String? _sessionReply;

  int _heardCount = 0;
  final Map<String, _NearbyHit> _nearby = {};

  static final Guid kLmycService = Guid(kLmycServiceUuid);
  static final Guid kLmycPayloadChr = Guid(kLmycPayloadUuid);
  static final Guid kLmycSessionChr = Guid(kLmycSessionUuid);
  static final Guid kLmycStatusChr = Guid(kLmycStatusUuid);
  static final Guid kLmycWifiChr = Guid(kLmycWifiUuid);

  bool _wifiBusy = false;
  bool _wifiJoined = false;

  /// The user saved the boat network on the Android prompt.
  bool _wifiSaved = false;
  bool _probing = false;
  String _wifiDetail = '';
  bool? _boatReachable;

  _Landing _landing = _Landing.start;
  String? _lastBoat;
  _NearbyHit? _autoTarget;
  Timer? _countdownTimer;

  /// Share of the auto-join countdown still to run, 1 down to 0.
  double _countdownLeft = 0;
  static const _countdown = Duration(seconds: 5);

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    unawaited(_startup());
  }

  /// A QR link wins. Otherwise a phone that joined before looks for that boat.
  Future<void> _startup() async {
    await _initDeepLinks();
    _lastBoat = await LastBoat.load();
    if (!mounted || _lastBoat == null) return;
    if (_oob == null && _blePhase == BlePhase.idle) unawaited(_discover());
  }

  /// Back from Wi-Fi settings: check whether the phone moved to the boat.
  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    final info = BoatSession.info;
    if (state != AppLifecycleState.resumed || info == null) return;
    if (_blePhase != BlePhase.paired || _wifiBusy || _boatReachable == true) {
      return;
    }
    unawaited(_waitForBoat(info.ip, const Duration(seconds: 8)));
  }

  Future<void> _scanQr() async {
    final uri = await Navigator.of(context)
        .push<Uri>(MaterialPageRoute(builder: (_) => const QrScanPage()));
    if (uri != null && mounted) _handleIncomingLink(uri);
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

  @override
  void setState(VoidCallback fn) {
    super.setState(fn);
    _ui.ping();
  }

  bool get _busy =>
      _blePhase == BlePhase.scanning ||
      _blePhase == BlePhase.connecting ||
      _blePhase == BlePhase.confirming ||
      _blePhase == BlePhase.checkingPermissions;

  void _handleIncomingLink(Uri uri) {
    if (uri.scheme != 'lmyc') return;
    final link = PairingLink.parse(uri);
    if (link == null || link.oob == null || link.bleName == null) {
      setState(() {
        _blePhase = BlePhase.failed;
        _bleDetail = link == null
            ? PairingLink.rejection(uri)
            : 'The terminal QR is missing a pairing secret.';
      });
      return;
    }
    _countdownTimer?.cancel();
    setState(() {
      _landing = _Landing.start;
      _boatId = link.boatId;
      _terminalId = link.terminalId;
      _bleName = link.bleName;
      _oob = link.oob;
    });
    _queueAutoConnect();
  }

  /// A QR scan opens the app. Connecting starts on its own.
  void _queueAutoConnect() {
    if (_connectQueued || _busy || _blePhase == BlePhase.paired) return;
    if (_oob == null || _bleName == null) return;
    _connectQueued = true;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _connectQueued = false;
      if (!mounted || _busy || _blePhase == BlePhase.paired) return;
      if (_oob == null || _bleName == null) return;
      unawaited(_startScan(autoConnectExact: true));
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
    final prefixMatch = names.any(isLmycBleName);
    final exactMatch = _bleName != null && names.any((n) => n == _bleName);
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
          _bleDetail =
              'Found ${found.advertisementData.advName.isNotEmpty ? found.advertisementData.advName : _bleName} '
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

  /// Listens for terminals for a few seconds. The last boat, or a lone
  /// terminal, starts the auto-join countdown; several go to a list.
  Future<void> _discover() async {
    _countdownTimer?.cancel();
    if (_busy) return;
    await _disconnect(resetPhase: false);
    setState(() => _landing = _Landing.searching);
    if (!await _ensurePermissions()) {
      setState(() => _landing = _Landing.start);
      return;
    }
    setState(() {
      _blePhase = BlePhase.scanning;
      _bleDetail = 'Looking for boat terminals…';
      _device = null;
      _rssi = null;
      _sessionReply = null;
      _heardCount = 0;
      _nearby.clear();
    });

    try {
      await FlutterBluePlus.stopScan();
      await _scanSubscription?.cancel();
      final sawLast = Completer<void>();
      _scanSubscription = FlutterBluePlus.scanResults.listen((results) {
        _heardCount = results.length;
        for (final r in results) {
          if (!_isLmycHit(r)) continue;
          final hit = _NearbyHit(r);
          _nearby[hit.id] = hit;
          if (hit.name == _lastBoat && !sawLast.isCompleted) {
            sawLast.complete();
          }
        }
        if (mounted) setState(() {});
      });
      await FlutterBluePlus.startScan(
        timeout: const Duration(seconds: 4),
        androidUsesFineLocation: false,
        androidScanMode: AndroidScanMode.lowLatency,
      );
      await Future.any<void>([
        sawLast.future,
        FlutterBluePlus.isScanning.firstWhere((v) => !v).then((_) {}),
      ]).timeout(const Duration(seconds: 6), onTimeout: () {});
      await FlutterBluePlus.stopScan();
      await _scanSubscription?.cancel();
      _scanSubscription = null;
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _landing = _Landing.none;
        _blePhase = BlePhase.idle;
        _bleDetail = 'Scan error: $e';
      });
      return;
    }

    if (!mounted) return;
    final hits = _sortedHits();
    final target =
        hits.where((h) => h.name == _lastBoat).firstOrNull ??
        (hits.length == 1 ? hits.first : null);
    setState(() {
      _blePhase = BlePhase.idle;
      _bleDetail = hits.isEmpty
          ? 'Heard $_heardCount BLE device(s), 0 named LMYC-*.'
          : 'Found ${hits.length} LMYC terminal(s).';
      _landing = hits.isEmpty ? _Landing.none : _Landing.choose;
    });
    if (target != null) _startCountdown(target);
  }

  List<_NearbyHit> _sortedHits() =>
      _nearby.values.toList()..sort((a, b) => b.rssi.compareTo(a.rssi));

  void _startCountdown(_NearbyHit hit) {
    _countdownTimer?.cancel();
    final started = DateTime.now();
    setState(() {
      _autoTarget = hit;
      _landing = _Landing.countdown;
      _countdownLeft = 1;
    });
    _countdownTimer = Timer.periodic(const Duration(milliseconds: 100), (t) {
      if (!mounted) {
        t.cancel();
        return;
      }
      final spent = DateTime.now().difference(started).inMilliseconds;
      final left = 1 - spent / _countdown.inMilliseconds;
      if (left > 0) {
        setState(() => _countdownLeft = left);
        return;
      }
      t.cancel();
      _countdownTimer = null;
      unawaited(_join(hit));
    });
  }

  void _cancelCountdown() {
    _countdownTimer?.cancel();
    _countdownTimer = null;
    setState(() => _landing = _Landing.choose);
  }

  /// Pairs with a terminal chosen on the landing page. The pairing secret is
  /// read from the terminal itself, so no QR is needed.
  Future<void> _join(_NearbyHit hit) async {
    _countdownTimer?.cancel();
    _countdownTimer = null;
    setState(() {
      _landing = _Landing.start;
      _bleName = hit.name;
      _boatId = TerminalName.parse(hit.name).boat;
      _terminalId = null;
      _oob = null;
      _device = hit.result.device;
      _rssi = hit.rssi;
      _blePhase = BlePhase.connecting;
      _bleDetail = 'Connecting to ${hit.name}…';
    });
    await _connectAndConfirm(hit.result.device);
  }

  /// The failed page's retry: the same terminal again, or a fresh Discover.
  void _retry() {
    if (_bleName != null) {
      unawaited(_startScan(autoConnectExact: true));
    } else {
      unawaited(_discover());
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

  Future<List<BluetoothService>> _discoverWithRetry(
    BluetoothDevice device,
  ) async {
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
            (_blePhase == BlePhase.paired ||
                _blePhase == BlePhase.confirming)) {
          setState(() {
            _blePhase = BlePhase.failed;
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
      BluetoothCharacteristic? statusChr;
      BluetoothCharacteristic? payloadChr;

      for (final s in services) {
        debugPrint('service: ${s.uuid}');
        if (s.uuid == kLmycService) {
          for (final c in s.characteristics) {
            debugPrint('  char: ${c.uuid}');
            if (c.uuid == kLmycSessionChr) sessionChr = c;
            if (c.uuid == kLmycStatusChr) statusChr = c;
            if (c.uuid == kLmycPayloadChr) payloadChr = c;
          }
        }
      }

      if (payloadChr != null) {
        try {
          final payload = await payloadChr.read();
          debugPrint('payload bytes: ${payload.length}');
          // The payload is the live lmyc:// URI (includes current oob=).
          // Prefer it over a stale QR after terminal reboot, and take the
          // boat and terminal IDs from it after a Discover.
          final link = linkFromPayloadBytes(payload);
          final fromPayload = link?.oob;
          if (fromPayload != null) {
            _oob = fromPayload;
            debugPrint(
              'OOB from payload URI (${fromPayload.length} hex chars)',
            );
          }
          _boatId = link?.boatId ?? _boatId;
          _terminalId = link?.terminalId ?? _terminalId;
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

      if (statusChr == null) {
        setState(() {
          _blePhase = BlePhase.failed;
          _bleDetail =
              'Terminal firmware is too old (no status char …000021).\n'
              'Run: make flash   then rescan QR and pair again.';
        });
        return;
      }

      if (_oob == null) {
        setState(() {
          _blePhase = BlePhase.idle;
          _sessionReply = 'WAIT (no OOB yet)';
          _bleDetail = 'Link is up. Scan the terminal QR (or paste lmyc://) then tap Pair.';
        });
        return;
      }

      // Default ATT MTU is 23 → max write payload 20 bytes. Firmware accepts
      // a raw 16-byte OOB write on session; status comes from …000021.
      final oobBytes = oobBytesFromHex(_oob!);
      if (oobBytes == null) {
        setState(() {
          _blePhase = BlePhase.failed;
          _bleDetail =
              'OOB from QR is not 32 hex chars (got ${_oob!.length}). Rescan QR.';
        });
        return;
      }

      try {
        await device.clearGattCache();
      } catch (e) {
        debugPrint('clearGattCache: $e');
      }

      final statusCompleter = Completer<String>();
      StreamSubscription<List<int>>? notifySub;
      try {
        await statusChr.setNotifyValue(true);
        notifySub = statusChr.onValueReceived.listen((bytes) {
          final text = sessionReplyFromBytes(bytes);
          debugPrint('status notify: ${bytes.length}B raw=$bytes text="$text"');
          if (isSessionStatus(text) && !statusCompleter.isCompleted) {
            statusCompleter.complete(text);
          }
        });
      } catch (e) {
        debugPrint('status notify enable failed: $e');
      }

      debugPrint('session write raw OOB: ${oobBytes.length} bytes');
      await sessionChr.write(oobBytes, withoutResponse: false);

      String reply = '';
      try {
        reply = await statusCompleter.future.timeout(
          const Duration(milliseconds: 2000),
        );
      } on TimeoutException {
        for (var attempt = 0; attempt < 8; attempt++) {
          await Future<void>.delayed(
            Duration(milliseconds: attempt == 0 ? 200 : 150),
          );
          final replyBytes = await statusChr.read();
          reply = sessionReplyFromBytes(replyBytes);
          debugPrint(
            'status read #$attempt: ${replyBytes.length}B '
            'raw=$replyBytes text="$reply"',
          );
          if (isSessionStatus(reply)) break;
        }
      } finally {
        await notifySub?.cancel();
        try {
          await statusChr.setNotifyValue(false);
        } catch (_) {}
      }

      if (!mounted) return;

      if (reply == 'OK') {
        final name = _bleName;
        if (name != null) {
          _lastBoat = name;
          unawaited(LastBoat.save(name));
        }
        setState(() {
          _blePhase = BlePhase.paired;
          _sessionReply = reply;
          _bleDetail =
              'Paired with ${_boatId ?? "boat"}\nTerminal confirmed OOB.';
        });
        await _bringUpBoatWifi(device);
      } else {
        final shown = isSessionStatus(reply) ? reply : '(no status yet)';
        setState(() {
          _blePhase = BlePhase.failed;
          _sessionReply = shown;
          _bleDetail =
              'Terminal rejected OOB (status: $shown).\n'
              'Rescan the QR — OOB rotates on terminal reboot.';
        });
      }
    } catch (e) {
      if (!mounted) return;
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
        _oob = null;
        _landing = _Landing.start;
        if (!_wifiJoined) {
          _wifiDetail = '';
          _boatReachable = null;
        }
      });
    }
  }

  /// After OOB OK, read the SoftAP details and ask Android to join that network.
  Future<void> _bringUpBoatWifi(BluetoothDevice device) async {
    if (_wifiBusy) {
      return;
    }
    setState(() {
      _wifiBusy = true;
      _wifiJoined = false;
      _wifiSaved = false;
      _boatReachable = null;
      _wifiDetail = 'Reading the boat Wi-Fi name over Bluetooth…';
    });
    try {
      final status = await Permission.nearbyWifiDevices.request();
      if (!status.isGranted && !status.isLimited) {
        debugPrint('nearby wifi permission: $status');
      }
      try {
        await device.requestMtu(185);
      } catch (e) {
        debugPrint('requestMtu: $e');
      }
      final services = await device.discoverServices();
      BluetoothCharacteristic? wifiChr;
      for (final s in services) {
        if (s.uuid != kLmycService) continue;
        for (final c in s.characteristics) {
          if (c.uuid == kLmycWifiChr) wifiChr = c;
        }
      }
      if (wifiChr == null) {
        if (!mounted) return;
        setState(() {
          _wifiDetail = 'This terminal has no Wi-Fi bootstrap yet. Flash the new firmware and pair again.';
        });
        return;
      }
      final raw = utf8.decode(await wifiChr.read(), allowMalformed: true);
      final info = parseWifiBootstrap(raw);
      if (info == null) {
        if (!mounted) return;
        setState(() {
          _wifiDetail = 'The terminal did not share Wi-Fi yet. Tap Join boat Wi-Fi to ask again.';
        });
        return;
      }
      BoatSession.offer(info);
      if (!mounted) return;
      setState(() {
        _wifiDetail =
            'Android is asking to join ${info.ssid}. Tap Connect. '
            'The network has no internet; Read Book still uses ${info.ip}.';
      });
      final saved = await BoatWifi.join(ssid: info.ssid, psk: info.psk);
      if (!mounted) return;
      if (!saved) {
        setState(() {
          _wifiDetail =
              'The Android prompt was closed without saving ${info.ssid}. '
              'Tap Join boat Wi-Fi to see it again.';
        });
        return;
      }
      BoatSession.markJoined();
      setState(() {
        _wifiSaved = true;
        _wifiDetail = 'Waiting for the phone to move to ${info.ssid}…';
      });
      await _waitForBoat(info.ip, const Duration(seconds: 12));
    } on PlatformException catch (e) {
      if (!mounted) return;
      setState(() {
        _wifiJoined = false;
        _wifiDetail = e.message ?? 'Could not join the boat Wi-Fi.';
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _wifiJoined = false;
        _wifiDetail = 'Could not join the boat Wi-Fi ($e).';
      });
    } finally {
      if (mounted) {
        setState(() => _wifiBusy = false);
      } else {
        _wifiBusy = false;
      }
    }
  }

  /// Tries port 10110 until it opens or [patience] runs out. Android takes a
  /// few seconds to move Wi-Fi after a save, so one quick try is not enough.
  Future<void> _waitForBoat(String ip, Duration patience) async {
    if (_probing) return;
    setState(() => _probing = true);
    final deadline = DateTime.now().add(patience);
    var open = false;
    try {
      while (mounted) {
        try {
          final socket = await Socket.connect(
            ip,
            kGatewayTcpPort,
            timeout: const Duration(seconds: 2),
          );
          socket.destroy();
          open = true;
          break;
        } catch (_) {
          if (DateTime.now().isAfter(deadline)) break;
          await Future<void>.delayed(const Duration(seconds: 1));
        }
      }
    } finally {
      _probing = false;
    }
    if (!mounted) return;
    final ssid = BoatSession.ssid;
    setState(() {
      _boatReachable = open;
      _wifiJoined = open;
      _wifiDetail = open
          ? 'The phone is on the boat hotspot. Navionics and i-Boating can use $ip port $kGatewayTcpPort, and UDP $kGatewayUdpPort and $kGatewayUdpAltPort.'
          : 'Android saved $ssid but stays on a Wi-Fi network that has internet. '
                'Tap Open Wi-Fi settings and choose $ssid. '
                'Away from other Wi-Fi, on the boat, the phone joins it by itself.';
    });
  }

  /// The one Wi-Fi step left, or null when the phone already reaches the boat.
  Widget? _wifiAction() {
    if (_blePhase != BlePhase.paired || _boatReachable == true) return null;
    final style = FilledButton.styleFrom(
      backgroundColor: const Color(0xFF001F3F),
      padding: const EdgeInsets.symmetric(vertical: 14),
    );
    if (_wifiSaved) {
      return FilledButton.icon(
        onPressed: _probing ? null : () => unawaited(_openWifiSettings()),
        icon: const Icon(Icons.wifi),
        label: Text(_probing ? 'Checking…' : 'Open Wi-Fi settings'),
        style: style,
      );
    }
    final device = _device;
    return FilledButton.icon(
      onPressed: _wifiBusy || device == null
          ? null
          : () => unawaited(_bringUpBoatWifi(device)),
      icon: const Icon(Icons.wifi),
      label: Text(_wifiBusy ? 'Joining…' : 'Join boat Wi-Fi'),
      style: style,
    );
  }

  Future<void> _openWifiSettings() async {
    try {
      await BoatWifi.openSettings();
    } on PlatformException catch (e) {
      if (!mounted) return;
      setState(
        () => _wifiDetail = e.message ?? 'Could not open Wi-Fi settings.',
      );
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _countdownTimer?.cancel();
    unawaited(_linkSubscription?.cancel());
    unawaited(_scanSubscription?.cancel());
    unawaited(_connSubscription?.cancel());
    _pasteController.dispose();
    unawaited(_disconnect(resetPhase: false));
    _ui.dispose();
    super.dispose();
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

  double get _progress {
    if (_wifiBusy) return 0.9;
    switch (_blePhase) {
      case BlePhase.checkingPermissions:
        return 0.15;
      case BlePhase.scanning:
        return 0.4;
      case BlePhase.connecting:
        return 0.6;
      case BlePhase.confirming:
        return 0.78;
      case BlePhase.paired:
        return 1;
      case BlePhase.failed:
      case BlePhase.idle:
        return _oob == null ? 0 : 0.08;
    }
  }

  String get _stepTitle {
    if (_wifiBusy) return 'Joining the boat Wi-Fi';
    switch (_blePhase) {
      case BlePhase.checkingPermissions:
        return 'Asking for Bluetooth';
      case BlePhase.scanning:
        return _bleName == null
            ? 'Looking for the terminal'
            : 'Looking for $_bleName';
      case BlePhase.connecting:
        return 'Connecting';
      case BlePhase.confirming:
        return 'Confirming this boat';
      case BlePhase.failed:
        final line = _bleDetail.split('\n').first.trim();
        return line.isEmpty ? 'Could not connect' : line;
      case BlePhase.paired:
        return 'Connected';
      case BlePhase.idle:
        return 'Scan the QR code on the terminal';
    }
  }

  void _openStatus() {
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (routeContext) {
          return ListenableBuilder(
            listenable: _ui,
            builder: (_, _) => _statusPage(routeContext),
          );
        },
      ),
    );
  }

  void _openNmea() {
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (routeContext) {
          return ListenableBuilder(
            listenable: _ui,
            builder: (_, _) => NmeaInfoPage(
              ssid: BoatSession.ssid,
              ip: BoatSession.ip,
              onBoat: _wifiJoined,
            ),
          );
        },
      ),
    );
  }

  void _openMaintenance() {
    Navigator.of(context).push(
      MaterialPageRoute<void>(builder: (_) => const MaintenanceMenuPage()),
    );
  }

  @override
  Widget build(BuildContext context) {
    if (_blePhase == BlePhase.paired && !_wifiBusy) {
      return _buildMenu(context);
    }
    if (_landing != _Landing.start ||
        (_blePhase == BlePhase.idle && _oob == null)) {
      return _buildWelcome(context);
    }
    return _buildConnect(context);
  }

  PreferredSizeWidget _bar(String title) {
    return AppBar(
      title: Text(title),
      actions: [
        TextButton(
          onPressed: _openStatus,
          child: const Text(
            'Diagnostics',
            style: TextStyle(color: Colors.white),
          ),
        ),
      ],
    );
  }

  Widget _buildWelcome(BuildContext context) {
    final Widget body = switch (_landing) {
      _Landing.start => _landingStart(context),
      _Landing.searching => _landingSearching(context),
      _Landing.countdown => _landingCountdown(context),
      _Landing.choose => _landingChoose(context),
      _Landing.none => _landingNone(context),
    };
    return Scaffold(
      appBar: _bar('LMYC'),
      body: SafeArea(child: body),
    );
  }

  Widget _heading(BuildContext context, String text) {
    return Text(
      text,
      textAlign: TextAlign.center,
      style: Theme.of(context).textTheme.headlineSmall?.copyWith(
        fontWeight: FontWeight.w700,
        color: const Color(0xFF001F3F),
      ),
    );
  }

  Widget _lead(BuildContext context, String text) {
    return Text(
      text,
      textAlign: TextAlign.center,
      style: Theme.of(context).textTheme.bodyLarge
          ?.copyWith(color: Colors.black54, height: 1.4),
    );
  }

  ButtonStyle get _primary => FilledButton.styleFrom(
    backgroundColor: const Color(0xFF001F3F),
    padding: const EdgeInsets.symmetric(vertical: 14),
  );

  Widget _scanQrButton() {
    return OutlinedButton.icon(
      onPressed: () => unawaited(_scanQr()),
      icon: const Icon(Icons.qr_code_scanner),
      label: const Text('Scan QR code'),
    );
  }

  /// Signal, the name on the terminal's BLE row, and why it is first.
  String _hitDetail(_NearbyHit hit, {required bool closest}) {
    final parts = [signalWord(hit.rssi), hit.name];
    if (hit.name == _lastBoat) parts.add('Last used');
    if (closest) parts.add('Closest');
    return parts.join(' · ');
  }

  Widget _landingStart(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(28),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const Spacer(),
          const Icon(Icons.sailing, size: 64, color: Color(0xFF001F3F)),
          const SizedBox(height: 20),
          _heading(context, 'Find your boat'),
          const SizedBox(height: 8),
          _lead(
            context,
            'Aboard, near the terminal, tap Discover. The app pairs over '
            'Bluetooth and sets up the boat Wi-Fi on its own.',
          ),
          const SizedBox(height: 28),
          FilledButton.icon(
            onPressed: () => unawaited(_discover()),
            icon: const Icon(Icons.radar),
            label: const Text('Discover'),
            style: _primary,
          ),
          const SizedBox(height: 8),
          _scanQrButton(),
          const Spacer(),
        ],
      ),
    );
  }

  Widget _landingSearching(BuildContext context) {
    final count = _nearby.length;
    return Padding(
      padding: const EdgeInsets.all(28),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const Spacer(),
          const Center(child: CircularProgressIndicator()),
          const SizedBox(height: 24),
          _heading(context, 'Looking for boat terminals'),
          const SizedBox(height: 8),
          _lead(
            context,
            count == 0 ? 'Listening over Bluetooth…' : 'Found $count so far…',
          ),
          const Spacer(),
        ],
      ),
    );
  }

  Widget _landingCountdown(BuildContext context) {
    final hit = _autoTarget!;
    final hits = _sortedHits();
    final seconds = (_countdownLeft * _countdown.inSeconds).ceil();
    return Padding(
      padding: const EdgeInsets.all(28),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const Spacer(),
          const Icon(Icons.sailing, size: 64, color: Color(0xFF001F3F)),
          const SizedBox(height: 20),
          _heading(context, TerminalName.parse(hit.name).title),
          const SizedBox(height: 8),
          _lead(
            context,
            _hitDetail(
              hit,
              closest: hits.length > 1 && hits.first.id == hit.id,
            ),
          ),
          const SizedBox(height: 28),
          Text(
            'Joining in $seconds s',
            textAlign: TextAlign.center,
            style: const TextStyle(fontSize: 16),
          ),
          const SizedBox(height: 10),
          InkWell(
            onTap: _cancelCountdown,
            borderRadius: BorderRadius.circular(8),
            child: Padding(
              padding: const EdgeInsets.symmetric(vertical: 8),
              child: ClipRRect(
                borderRadius: BorderRadius.circular(8),
                child: LinearProgressIndicator(
                  value: _countdownLeft,
                  minHeight: 10,
                  backgroundColor: const Color(0xFFE2E8F0),
                  color: const Color(0xFF001F3F),
                ),
              ),
            ),
          ),
          const SizedBox(height: 20),
          FilledButton(
            onPressed: () => unawaited(_join(hit)),
            style: _primary,
            child: const Text('Join now'),
          ),
          const SizedBox(height: 8),
          OutlinedButton(
            onPressed: _cancelCountdown,
            child: const Text('Cancel'),
          ),
          const Spacer(),
        ],
      ),
    );
  }

  Widget _landingChoose(BuildContext context) {
    final hits = _sortedHits();
    return ListView(
      padding: const EdgeInsets.fromLTRB(20, 28, 20, 24),
      children: [
        _heading(
          context,
          hits.length == 1
              ? 'One boat terminal nearby'
              : '${hits.length} boat terminals nearby',
        ),
        const SizedBox(height: 8),
        _lead(
          context,
          'The closest is usually the boat you are on. The name under each '
          'one matches BLE name on the terminal Diagnostics screen.',
        ),
        const SizedBox(height: 24),
        for (final (i, hit) in hits.indexed)
          HomeChoice(
            icon: Icons.sailing,
            title: TerminalName.parse(hit.name).title,
            subtitle: _hitDetail(hit, closest: i == 0 && hits.length > 1),
            onTap: () => unawaited(_join(hit)),
          ),
        const SizedBox(height: 8),
        _lead(context, 'Not in the list? Scan the QR code on the terminal.'),
        const SizedBox(height: 12),
        _scanQrButton(),
        TextButton(
          onPressed: () => unawaited(_discover()),
          child: const Text('Search again'),
        ),
      ],
    );
  }

  Widget _landingNone(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(28),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const Spacer(),
          const Icon(
            Icons.bluetooth_searching,
            size: 64,
            color: Color(0xFF001F3F),
          ),
          const SizedBox(height: 20),
          _heading(context, 'No boat terminal found'),
          const SizedBox(height: 8),
          _lead(
            context,
            'Check that Bluetooth is on and you are near the terminal, '
            'then search again. Or scan the QR code on its screen.',
          ),
          const SizedBox(height: 28),
          FilledButton.icon(
            onPressed: () => unawaited(_discover()),
            icon: const Icon(Icons.radar),
            label: const Text('Search again'),
            style: _primary,
          ),
          const SizedBox(height: 8),
          _scanQrButton(),
          const Spacer(),
        ],
      ),
    );
  }

  Widget _buildConnect(BuildContext context) {
    final failed = _blePhase == BlePhase.failed;
    return Scaffold(
      appBar: _bar('LMYC'),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(28),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const Spacer(),
              Icon(
                failed ? Icons.link_off : Icons.sailing,
                size: 64,
                color: failed ? Colors.red.shade700 : const Color(0xFF001F3F),
              ),
              const SizedBox(height: 20),
              Text(
                failed ? 'Could not connect' : 'Connecting',
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                  fontWeight: FontWeight.w700,
                  color: const Color(0xFF001F3F),
                ),
              ),
              const SizedBox(height: 12),
              Text(
                _stepTitle,
                textAlign: TextAlign.center,
                style: const TextStyle(fontSize: 16, height: 1.4),
              ),
              const SizedBox(height: 28),
              if (!failed)
                ClipRRect(
                  borderRadius: BorderRadius.circular(8),
                  child: LinearProgressIndicator(
                    value: _progress,
                    minHeight: 8,
                    backgroundColor: const Color(0xFFE2E8F0),
                    color: const Color(0xFF001F3F),
                  ),
                ),
              if (failed) ...[
                FilledButton(
                  onPressed: _busy ? null : _retry,
                  style: FilledButton.styleFrom(
                    backgroundColor: const Color(0xFF001F3F),
                    padding: const EdgeInsets.symmetric(vertical: 14),
                  ),
                  child: const Text('Try again'),
                ),
                const SizedBox(height: 8),
                OutlinedButton.icon(
                  onPressed: _busy ? null : () => unawaited(_scanQr()),
                  icon: const Icon(Icons.qr_code_scanner),
                  label: const Text('Scan the QR code'),
                ),
                TextButton(
                  onPressed: _busy ? null : () => unawaited(_discover()),
                  child: const Text('Choose another terminal'),
                ),
              ],
              const Spacer(),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildMenu(BuildContext context) {
    final boat = (_boatId == null || _boatId!.isEmpty) ? 'the boat' : _boatId!;
    final wifiAction = _wifiAction();
    return Scaffold(
      appBar: AppBar(title: Text(boat)),
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.fromLTRB(20, 28, 20, 24),
          children: [
            Text(
              'Connected',
              style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                fontWeight: FontWeight.w700,
                color: const Color(0xFF001F3F),
              ),
            ),
            if (wifiAction != null) ...[
              const SizedBox(height: 8),
              Text(
                _wifiDetail.isEmpty
                    ? 'The phone is not on the boat Wi-Fi yet.'
                    : _wifiDetail,
                style: const TextStyle(color: Colors.black54, height: 1.4),
              ),
              const SizedBox(height: 12),
              wifiAction,
            ],
            const SizedBox(height: 28),
            HomeChoice(
              icon: Icons.settings_ethernet,
              title: 'NMEA connection',
              subtitle: 'Wi-Fi, address, and ports',
              onTap: _openNmea,
            ),
            HomeChoice(
              icon: Icons.monitor_heart_outlined,
              title: 'Boat status',
              subtitle: 'Link and diagnostics',
              onTap: _openStatus,
            ),
            HomeChoice(
              icon: Icons.handyman_outlined,
              title: 'Maintenance',
              subtitle: 'Defects and the log',
              onTap: _openMaintenance,
            ),
          ],
        ),
      ),
    );
  }

  Widget _statusPage(BuildContext routeContext) {
    final hits = _nearby.values.toList()
      ..sort((a, b) => b.rssi.compareTo(a.rssi));
    final paired = _blePhase == BlePhase.paired;
    return Scaffold(
      appBar: AppBar(title: const Text('Boat status')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          Row(
            children: [
              Text(
                paired ? 'Connected' : _phaseLabel,
                style: Theme.of(routeContext).textTheme.titleLarge?.copyWith(
                  fontWeight: FontWeight.w700,
                  color: const Color(0xFF001F3F),
                ),
              ),
              const Spacer(),
              _badge(_phaseLabel, _phaseColor),
            ],
          ),
          const SizedBox(height: 6),
          Text(
            _boatId == null || _boatId!.isEmpty ? 'No boat yet' : _boatId!,
            style: const TextStyle(color: Colors.black54),
          ),
          const SizedBox(height: 20),
          for (final row in linkRows(
            blePaired: paired,
            info: BoatSession.info,
            joining: _wifiBusy && !_wifiJoined,
            joined: _wifiJoined,
            reached: _boatReachable,
          )) ...[_LinkStatusRow(row: row), const SizedBox(height: 8)],
          if (_wifiDetail.isNotEmpty) ...[
            const SizedBox(height: 8),
            Text(_wifiDetail, style: const TextStyle(height: 1.4)),
          ],
          const SizedBox(height: 16),
          _statusRow('Terminal', _terminalId),
          _statusRow('Bluetooth', _bleName),
          if (_rssi != null) _statusRow('Signal', '$_rssi dBm'),
          if (_sessionReply != null) _statusRow('Session', _sessionReply),
          if (_bleDetail.isNotEmpty) ...[
            const SizedBox(height: 4),
            Text(
              _bleDetail,
              style: TextStyle(color: _phaseColor, height: 1.35),
            ),
          ],
          const SizedBox(height: 16),
          ?_wifiAction(),
          if (_device != null) ...[
            const SizedBox(height: 8),
            OutlinedButton.icon(
              onPressed: () {
                Navigator.of(routeContext).push(
                  MaterialPageRoute<void>(
                    builder: (_) => NmeaConsolePage(
                      device: _device!,
                      terminalIp: BoatSession.ip,
                    ),
                  ),
                );
              },
              icon: const Icon(Icons.ssid_chart),
              label: const Text('Sentences'),
            ),
          ],
          const SizedBox(height: 8),
          OutlinedButton.icon(
            onPressed: () {
              Navigator.of(routeContext).push(
                MaterialPageRoute<void>(
                  builder: (_) => BoatLinkPage(suggestedIp: BoatSession.ip),
                ),
              );
            },
            icon: const Icon(Icons.photo_outlined),
            label: const Text('Chart snapshot'),
          ),
          if (!paired) ...[
            const SizedBox(height: 8),
            OutlinedButton.icon(
              onPressed: () {
                Navigator.of(routeContext).push(
                  MaterialPageRoute<void>(
                    builder: (_) =>
                        MaintenanceBookPage(suggestedIp: BoatSession.ip),
                  ),
                );
              },
              icon: const Icon(Icons.build_outlined),
              label: const Text('Maintenance book'),
            ),
          ],
          if (paired) ...[
            const SizedBox(height: 8),
            TextButton(
              onPressed: () => unawaited(_disconnect()),
              child: const Text('Disconnect'),
            ),
          ],
          const SizedBox(height: 12),
          ExpansionTile(
            title: const Text('Find a terminal'),
            childrenPadding: const EdgeInsets.fromLTRB(16, 0, 16, 12),
            children: [
              TextField(
                controller: _pasteController,
                decoration: const InputDecoration(
                  labelText: 'Paste an lmyc:// code',
                  border: OutlineInputBorder(),
                  isDense: true,
                ),
                minLines: 1,
                maxLines: 3,
                onSubmitted: (_) => _applyPastedUri(),
              ),
              Align(
                alignment: Alignment.centerRight,
                child: TextButton(
                  onPressed: _applyPastedUri,
                  child: const Text('Apply'),
                ),
              ),
              SizedBox(
                width: double.infinity,
                child: OutlinedButton.icon(
                  onPressed: _busy
                      ? null
                      : () => unawaited(_startScan(autoConnectExact: false)),
                  icon: const Icon(Icons.radar),
                  label: Text(_busy ? 'Working…' : 'Find terminals'),
                ),
              ),
              ...hits.map((h) {
                return ListTile(
                  contentPadding: EdgeInsets.zero,
                  title: Text(h.name),
                  subtitle: Text('${h.rssi} dBm'),
                  trailing: TextButton(
                    onPressed: _busy
                        ? null
                        : () {
                            setState(() {
                              _bleName = h.name;
                              _device = h.result.device;
                              _rssi = h.rssi;
                              _blePhase = BlePhase.connecting;
                              _bleDetail = 'Connecting to ${h.name}…';
                            });
                            unawaited(_connectAndConfirm(h.result.device));
                          },
                    child: const Text('Connect'),
                  ),
                );
              }),
            ],
          ),
        ],
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
