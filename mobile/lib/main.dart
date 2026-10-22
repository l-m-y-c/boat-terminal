import 'dart:async';
import 'dart:convert';

import 'package:app_links/app_links.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

import 'boat_setup.dart';
import 'boat_wifi.dart';
import 'home_flow.dart';
import 'instruments.dart';
import 'journey.dart';
import 'last_boat.dart';
import 'nmea_data_page.dart';
import 'nmea_console.dart';
import 'pairing_link.dart';
import 'protocol.dart';
import 'qr_scan_page.dart';
import 'terminal_stats.dart';
import 'ui_kit.dart';

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

/// The screens once on board, on the bottom bar.
enum OnBoardTab { boat, instruments, nmeaData }

/// How long "On board BOAT" shows after joining before the tabs appear.
const kArrivalSplash = Duration(milliseconds: 2200);

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
  static final Guid kLmycStatsChr = Guid(kLmycStatsUuid);

  /// Read every two seconds while paired: data source, bus, delivery.
  BluetoothCharacteristic? _statsChr;
  TerminalStats? _stats;
  DateTime? _statsAt;
  Timer? _statsTimer;
  bool _statsReading = false;

  bool _wifiReading = false;
  bool _wifiChecking = false;
  String? _wifiProblem;
  BoatWifiStatus? _wifiStatus;
  Timer? _wifiTimer;

  _Landing _landing = _Landing.start;
  String? _lastBoat;
  _NearbyHit? _autoTarget;
  Timer? _countdownTimer;

  OnBoardTab _tab = OnBoardTab.boat;

  /// The boat whose arrival splash has been shown, so a Wi-Fi blip that
  /// drops out of Arrived and back does not show it again.
  String? _splashShownFor;
  bool _splashing = false;
  Timer? _splashTimer;

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

  /// Back from Wi-Fi settings, look at once. In the background, stop looking.
  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) {
      _startWifiWatch();
      unawaited(_checkWifi());
    } else if (state == AppLifecycleState.paused) {
      _stopWifiWatch();
    }
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
            _stopStats();
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
      BluetoothCharacteristic? statsChr;
      BluetoothCharacteristic? payloadChr;

      for (final s in services) {
        debugPrint('service: ${s.uuid}');
        if (s.uuid == kLmycService) {
          for (final c in s.characteristics) {
            debugPrint('  char: ${c.uuid}');
            if (c.uuid == kLmycSessionChr) sessionChr = c;
            if (c.uuid == kLmycStatusChr) statusChr = c;
            if (c.uuid == kLmycStatsChr) statsChr = c;
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
        _startStats(statsChr);
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
    _stopStats();
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
        _wifiProblem = null;
        _tab = OnBoardTab.boat;
        _splashShownFor = null;
        _splashing = false;
      });
    }
  }

  /// After OOB OK: read the hotspot details, save them unless the phone
  /// already uses the boat Wi-Fi, then keep watching until chart apps can
  /// reach the terminal.
  Future<void> _bringUpBoatWifi(BluetoothDevice device) async {
    if (_wifiReading) return;
    setState(() {
      _wifiReading = true;
      _wifiProblem = null;
    });
    BoatWifiInfo? info;
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
        _wifiProblem = 'This terminal does not share its Wi-Fi. Its software needs an update.';
      } else {
        final raw = utf8.decode(await wifiChr.read(), allowMalformed: true);
        info = parseWifiBootstrap(raw);
        if (info == null) {
          _wifiProblem = 'The terminal did not send its Wi-Fi details.';
        }
      }
    } catch (e) {
      debugPrint('wifi bootstrap: $e');
      _wifiProblem = 'Lost the Bluetooth link while reading the Wi-Fi details.';
    }
    if (!mounted) return;
    setState(() => _wifiReading = false);
    if (info == null) return;

    BoatSession.offer(info);
    await _startRelay(info);
    _startWifiWatch();
    await _checkWifi();
  }

  /// Chart apps read 127.0.0.1:10110. The notification is the only sign the
  /// relay is running, so ask for it first; the relay works without it.
  Future<void> _startRelay(BoatWifiInfo info) async {
    try {
      await Permission.notification.request();
      // A refused Connect dialog is not asked again by a running service.
      if (_wifiStatus?.relayDeclined == true) await BoatWifi.stopRelay();
      await BoatWifi.startRelay(ip: info.ip, ssid: info.ssid, psk: info.psk);
    } on PlatformException catch (e) {
      debugPrint('relay: ${e.message}');
    }
  }

  /// One look at the phone's networks. Cheap: it only opens a socket when
  /// the phone has an address on the boat hotspot.
  Future<void> _checkWifi() async {
    final info = BoatSession.info;
    if (info == null || _wifiChecking) return;
    _wifiChecking = true;
    try {
      final status = await BoatWifi.status(ip: info.ip);
      if (!mounted) return;
      if (status.ready) BoatSession.markJoined();
      // The relay came back without the Wi-Fi details: hand them over again.
      if (status.relayNeedsApp) unawaited(_startRelay(info));
      if (status != _wifiStatus) {
        debugPrint('boat wifi: $status');
        setState(() => _wifiStatus = status);
      }
    } on PlatformException catch (e) {
      debugPrint('boat wifi status: ${e.message}');
    } finally {
      _wifiChecking = false;
    }
  }

  /// Android can move the phone between networks at any time, so the check
  /// runs for as long as the app is on screen, not only during setup.
  void _startWifiWatch() {
    if (_wifiTimer != null || BoatSession.info == null) return;
    _wifiTimer = Timer.periodic(
      const Duration(seconds: 2),
      (_) => unawaited(_checkWifi()),
    );
  }

  void _stopWifiWatch() {
    _wifiTimer?.cancel();
    _wifiTimer = null;
  }

  WifiSetup get _wifiSetup => wifiSetupFor(
    reading: _wifiReading,
    info: BoatSession.info,
    status: _wifiStatus,
    problem: _wifiProblem,
  );

  bool get _wifiReady => _wifiStatus?.ready == true;

  Future<void> _onSetupAction(SetupAction action) async {
    try {
      switch (action) {
        case SetupAction.retry:
          final device = _device;
          final info = BoatSession.info;
          if (device != null) {
            await _bringUpBoatWifi(device);
          } else if (info != null) {
            // No Bluetooth link now, but the Wi-Fi details are known: ask
            // Android again rather than leave Try again doing nothing.
            await _startRelay(info);
            await _checkWifi();
          }
      }
    } on PlatformException catch (e) {
      if (!mounted) return;
      setState(
        () =>
            _wifiProblem = e.message ?? 'Android did not open Wi-Fi settings.',
      );
    }
  }

  void _startStats(BluetoothCharacteristic? chr) {
    _stopStats();
    _statsChr = chr;
    if (chr == null) return;
    unawaited(_readStats());
    _statsTimer = Timer.periodic(
      const Duration(seconds: 2),
      (_) => unawaited(_readStats()),
    );
  }

  void _stopStats() {
    _statsTimer?.cancel();
    _statsTimer = null;
    _statsChr = null;
    _stats = null;
    _statsAt = null;
  }

  Future<void> _readStats() async {
    final chr = _statsChr;
    if (chr == null || _statsReading || _blePhase != BlePhase.paired) return;
    _statsReading = true;
    try {
      final next = TerminalStats.parse(
        utf8.decode(await chr.read(), allowMalformed: true),
      );
      if (next != null && mounted && _statsChr == chr) {
        setState(() {
          _stats = next;
          _statsAt = DateTime.now();
        });
      }
    } catch (e) {
      debugPrint('stats read: $e');
    } finally {
      _statsReading = false;
    }
  }

  void _openTab(OnBoardTab tab) {
    _splashTimer?.cancel();
    setState(() {
      _splashing = false;
      _tab = tab;
    });
  }

  /// Arrived for the first time with this boat: show the splash briefly.
  void _noteArrival(JourneyStop stop) {
    if (stop != JourneyStop.arrived) return;
    final boat = _boatTitle;
    if (_splashShownFor == boat) return;
    _splashShownFor = boat;
    _splashing = true;
    _tab = OnBoardTab.boat;
    _splashTimer?.cancel();
    _splashTimer = Timer(kArrivalSplash, () {
      if (mounted) setState(() => _splashing = false);
    });
  }

  @override
  void dispose() {
    _statsTimer?.cancel();
    _splashTimer?.cancel();
    _stopWifiWatch();
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
              onBoat: _wifiReady,
              simulated: _stats?.simulated ?? false,
            ),
          );
        },
      ),
    );
  }

  /// Where the member is. Arrival is about chart data, so a Bluetooth drop
  /// after that does not send them back to the start.
  JourneyStop get _stop {
    if (BoatSession.info != null && _wifiReady) return JourneyStop.arrived;
    if (_blePhase == BlePhase.paired) {
      return _wifiSetup == WifiSetup.noAnswer
          ? JourneyStop.charts
          : JourneyStop.wifi;
    }
    if (_landing != _Landing.start ||
        (_blePhase == BlePhase.idle && _oob == null)) {
      return JourneyStop.find;
    }
    return JourneyStop.connect;
  }

  bool get _stopProblem => switch (_stop) {
    JourneyStop.connect => _blePhase == BlePhase.failed,
    JourneyStop.wifi => _wifiSetup == WifiSetup.problem,
    _ => false,
  };

  String get _boatTitle {
    final id = _boatId;
    if (id != null && id.isNotEmpty) return id;
    final name = _bleName;
    if (name != null) {
      final boat = TerminalName.parse(name).boat;
      if (boat != null) return boat;
    }
    return 'LMYC';
  }

  @override
  Widget build(BuildContext context) {
    final stop = _stop;
    _noteArrival(stop);
    final onBoard = stop == JourneyStop.arrived && !_splashing;
    final contentKey = stop == JourneyStop.arrived
        ? (_splashing ? 'splash' : 'tab/${_tab.name}')
        : '$stop/${_landing.name}/${_wifiSetup.name}';
    return Scaffold(
      backgroundColor: const Color(0xFFF7F9FB),
      appBar: AppBar(
        title: BoatTitle(
          name: _boatTitle,
          simulated: _stats?.simulated ?? false,
        ),
        actions: [
          IconButton(
            tooltip: 'Diagnostics',
            onPressed: _openStatus,
            icon: const Icon(Icons.settings_outlined),
          ),
        ],
      ),
      body: SafeArea(
        child: Column(
          children: [
            AnimatedPadding(
              duration: const Duration(milliseconds: 250),
              padding: onBoard
                  ? const EdgeInsets.fromLTRB(28, 10, 28, 2)
                  : const EdgeInsets.fromLTRB(16, 18, 16, 6),
              child: JourneyBar(
                current: stop,
                problem: _stopProblem,
                compact: onBoard,
              ),
            ),
            Expanded(
              child: AnimatedSwitcher(
                duration: const Duration(milliseconds: 220),
                child: KeyedSubtree(
                  key: ValueKey(contentKey),
                  child: switch (stop) {
                    JourneyStop.find => _findPanel(context),
                    JourneyStop.connect => _connectPanel(),
                    JourneyStop.wifi || JourneyStop.charts => _wifiPanel(),
                    JourneyStop.arrived =>
                      _splashing ? _arrivalSplash(context) : _onBoardTab(),
                  },
                ),
              ),
            ),
          ],
        ),
      ),
      bottomNavigationBar: onBoard
          ? NavigationBar(
              selectedIndex: _tab.index,
              onDestinationSelected: (i) => _openTab(OnBoardTab.values[i]),
              destinations: const [
                NavigationDestination(
                  icon: Icon(Icons.sailing_outlined),
                  selectedIcon: Icon(Icons.sailing),
                  label: 'Boat',
                ),
                NavigationDestination(
                  icon: Icon(Icons.explore_outlined),
                  selectedIcon: Icon(Icons.explore),
                  label: 'Instruments',
                ),
                NavigationDestination(
                  icon: Icon(Icons.analytics_outlined),
                  selectedIcon: Icon(Icons.analytics),
                  label: 'NMEA data',
                ),
              ],
            )
          : null,
    );
  }

  Widget _onBoardTab() => switch (_tab) {
    OnBoardTab.boat => _boatTab(context),
    OnBoardTab.instruments => const InstrumentsView(),
    OnBoardTab.nmeaData => NmeaDataView(
      stats: _stats,
      updatedAt: _statsAt,
      supported: _statsChr != null || _stats != null,
    ),
  };

  /// Signal, the name on the terminal's BLE row, and why it is first.
  String _hitDetail(_NearbyHit hit, {required bool closest}) {
    final parts = [signalWord(hit.rssi), hit.name];
    if (hit.name == _lastBoat) parts.add('Last used');
    if (closest) parts.add('Closest');
    return parts.join(' · ');
  }

  Widget _scanButton({bool primary = false}) => primary
      ? PrimaryButton(
          label: 'Scan QR code',
          icon: Icons.qr_code_scanner,
          onPressed: () => unawaited(_scanQr()),
        )
      : SecondaryButton(
          label: 'Scan QR code',
          icon: Icons.qr_code_scanner,
          onPressed: () => unawaited(_scanQr()),
        );

  Widget _findPanel(BuildContext context) {
    switch (_landing) {
      case _Landing.start:
        return JourneyPanel(
          icon: Icons.sailing,
          title: 'Find your boat',
          body:
              'Scan the QR code on the boat\'s screen. '
              'Been aboard before? The app finds the boat by itself.',
          primary: _scanButton(primary: true),
          secondary: [
            SecondaryButton(
              label: 'Find nearby boats',
              icon: Icons.radar,
              onPressed: () => unawaited(_discover()),
            ),
          ],
        );
      case _Landing.searching:
        final count = _nearby.length;
        return JourneyPanel(
          icon: Icons.radar,
          title: 'Looking for boats',
          body: count == 0
              ? 'Listening for boat terminals nearby.'
              : 'Found $count so far.',
          working: true,
        );
      case _Landing.countdown:
        final hit = _autoTarget!;
        final hits = _sortedHits();
        final seconds = (_countdownLeft * _countdown.inSeconds).ceil();
        return JourneyPanel(
          icon: Icons.sailing,
          title: TerminalName.parse(hit.name).title,
          body:
              '${_hitDetail(hit, closest: hits.length > 1 && hits.first.id == hit.id)}'
              '\nJoining in $seconds s',
          progress: 1 - _countdownLeft,
          primary: PrimaryButton(
            label: 'Join now',
            onPressed: () => unawaited(_join(hit)),
          ),
          secondary: [
            SecondaryButton(
              label: 'Not this boat',
              onPressed: _cancelCountdown,
            ),
          ],
        );
      case _Landing.choose:
        final hits = _sortedHits();
        return ListView(
          padding: const EdgeInsets.fromLTRB(20, 16, 20, 24),
          children: [
            Text(
              hits.length == 1
                  ? 'One boat nearby'
                  : '${hits.length} boats nearby',
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.headlineSmall
                  ?.copyWith(fontWeight: FontWeight.w700, color: kNavy),
            ),
            const SizedBox(height: 8),
            const Text(
              'The closest is usually the boat you are on.',
              textAlign: TextAlign.center,
              style: TextStyle(color: Colors.black54, height: 1.4),
            ),
            const SizedBox(height: 20),
            for (final (i, hit) in hits.indexed)
              OptionTile(
                icon: Icons.sailing,
                title: TerminalName.parse(hit.name).title,
                subtitle: _hitDetail(hit, closest: i == 0 && hits.length > 1),
                onTap: () => unawaited(_join(hit)),
              ),
            const SizedBox(height: 8),
            _scanButton(),
            TextButton(
              onPressed: () => unawaited(_discover()),
              child: const Text('Search again'),
            ),
          ],
        );
      case _Landing.none:
        return JourneyPanel(
          icon: Icons.bluetooth_searching,
          title: 'No boat found nearby',
          body:
              'Make sure Bluetooth is on and you are aboard, '
              'or scan the QR code on the boat\'s screen.',
          primary: _scanButton(primary: true),
          secondary: [
            SecondaryButton(
              label: 'Search again',
              icon: Icons.radar,
              onPressed: () => unawaited(_discover()),
            ),
          ],
        );
    }
  }

  Widget _connectPanel() {
    if (_blePhase == BlePhase.failed) {
      final line = _bleDetail.split('\n').first.trim();
      return JourneyPanel(
        icon: Icons.link_off,
        tone: kRed,
        title: 'Could not connect',
        body: line.isEmpty
            ? 'Stay close to the terminal and try again.'
            : '$line. Stay close to the terminal and try again.',
        primary: PrimaryButton(
          label: 'Try again',
          onPressed: _busy ? null : _retry,
        ),
        secondary: [
          _scanButton(),
          TextButton(
            onPressed: _busy ? null : () => unawaited(_discover()),
            child: const Text('Choose another boat'),
          ),
        ],
      );
    }
    return JourneyPanel(
      icon: Icons.bluetooth_connected,
      title: 'Connecting to $_boatTitle',
      body: _stepTitle,
      progress: _progress,
    );
  }

  Widget _wifiPanel() {
    final ssid = BoatSession.ssid.isEmpty ? 'the boat Wi-Fi' : BoatSession.ssid;
    final setup = _wifiSetup;
    final declined = _wifiStatus?.relayDeclined == true;
    final copy = setupCopy(
      setup,
      ssid,
      problem: declined
          ? 'Android was not allowed to join $ssid. Tap Try again, then Connect.'
          : _wifiProblem,
    );
    final action = copy.action;
    return JourneyPanel(
      icon: setup == WifiSetup.noAnswer ? Icons.sensors : Icons.wifi,
      tone: setup == WifiSetup.problem ? kRed : kNavy,
      title: copy.title,
      body: copy.body,
      working: copy.working,
      primary: action == null
          ? null
          : PrimaryButton(
              label: copy.actionLabel!,
              onPressed: () => unawaited(_onSetupAction(action)),
            ),
    );
  }

  /// The moment of arrival: a big tick and the boat's name, then the tabs.
  /// Tap to skip.
  Widget _arrivalSplash(BuildContext context) {
    return GestureDetector(
      key: const ValueKey('arrival-splash'),
      behavior: HitTestBehavior.opaque,
      onTap: () => _openTab(OnBoardTab.boat),
      child: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Container(
              width: 112,
              height: 112,
              decoration: BoxDecoration(
                color: kGreen.withValues(alpha: 0.1),
                shape: BoxShape.circle,
              ),
              child: const Icon(Icons.check_rounded, size: 68, color: kGreen),
            ),
            const SizedBox(height: 22),
            Text(
              'On board $_boatTitle',
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.headlineSmall
                  ?.copyWith(fontWeight: FontWeight.w700, color: kNavy),
            ),
            const SizedBox(height: 8),
            const Text(
              'Chart apps on this phone can get the boat\'s data.',
              textAlign: TextAlign.center,
              style: TextStyle(color: Colors.black54, fontSize: 16),
            ),
          ],
        ),
      ),
    );
  }

  Widget _boatTab(BuildContext context) {
    final stats = _stats;
    final linked = _blePhase == BlePhase.paired;
    final state = stats?.state;
    final quiet = state == BusState.waiting || state == BusState.quiet;
    final fault = state == BusState.busOff || state == BusState.failed;
    return ListView(
      padding: const EdgeInsets.fromLTRB(20, 14, 20, 24),
      children: [
        Container(
          padding: const EdgeInsets.fromLTRB(16, 14, 16, 14),
          decoration: BoxDecoration(
            color: Colors.white,
            borderRadius: BorderRadius.circular(16),
            border: Border.all(color: kLine, width: 1.5),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              if (stats != null) ...[
                InkWell(
                  borderRadius: BorderRadius.circular(20),
                  onTap: () => showDataSourceSheet(context, stats),
                  child: DataStatusPill(stats: stats),
                ),
                const SizedBox(height: 10),
              ],
              Text(
                fault
                    ? 'Connected, but the boat\'s NMEA 2000 bus has a fault.'
                    : quiet
                    ? 'Connected. Waiting for data from the boat\'s instruments.'
                    : 'Chart apps on this phone can get the boat\'s data.',
                style: TextStyle(
                  color: fault ? kRed : Colors.black87,
                  fontSize: 15.5,
                  height: 1.4,
                ),
              ),
            ],
          ),
        ),
        if (!linked) ...[
          const SizedBox(height: 12),
          _Notice(
            color: Colors.black54,
            text: 'Bluetooth to the terminal dropped. Chart apps keep working.',
            action: TextButton(
              onPressed: _busy ? null : _retry,
              child: const Text('Reconnect'),
            ),
          ),
        ],
        const SizedBox(height: 22),
        const SectionLabel('On this phone'),
        const SizedBox(height: 10),
        OptionTile(
          icon: Icons.map_outlined,
          title: 'Connect a chart app',
          subtitle: 'Navionics or i-Boating: UDP, port 10110',
          onTap: _openNmea,
        ),
        OptionTile(
          icon: Icons.logout,
          title: 'Leave the boat',
          subtitle: 'Stop sharing the boat data on this phone',
          onTap: () => unawaited(_leaveBoat()),
        ),
      ],
    );
  }

  /// Back to the start: no relay, no watch, no Bluetooth.
  Future<void> _leaveBoat() async {
    _stopWifiWatch();
    try {
      await BoatWifi.stopRelay();
    } on PlatformException catch (e) {
      debugPrint('stop relay: ${e.message}');
    }
    BoatSession.clear();
    if (!mounted) return;
    setState(() => _wifiStatus = null);
    await _disconnect();
  }

  Widget _statusPage(BuildContext routeContext) {
    final hits = _nearby.values.toList()
      ..sort((a, b) => b.rssi.compareTo(a.rssi));
    final paired = _blePhase == BlePhase.paired;
    return Scaffold(
      appBar: AppBar(title: const Text('Diagnostics')),
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
            status: _wifiStatus,
          )) ...[_LinkStatusRow(row: row), const SizedBox(height: 8)],
          const SizedBox(height: 16),
          _statusRow('Terminal', _terminalId),
          if (_stats != null) _statusRow('Data', _stats!.pill),
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
          if (_device != null) ...[
            const SizedBox(height: 8),
            OutlinedButton.icon(
              onPressed: () {
                Navigator.of(routeContext).push(
                  MaterialPageRoute<void>(
                    builder: (_) => NmeaConsolePage(
                      device: _device!,
                      terminalIp: kRelayHost,
                    ),
                  ),
                );
              },
              icon: const Icon(Icons.ssid_chart),
              label: const Text('Sentences'),
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

/// A short line under the arrived header, tinted by what it is about.
class _Notice extends StatelessWidget {
  const _Notice({required this.color, required this.text, this.action});

  final Color color;
  final String text;
  final Widget? action;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.fromLTRB(14, 12, 8, 12),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.08),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: color.withValues(alpha: 0.3)),
      ),
      child: Row(
        children: [
          Expanded(
            child: Text(text, style: TextStyle(color: color, height: 1.35)),
          ),
          ?action,
        ],
      ),
    );
  }
}
