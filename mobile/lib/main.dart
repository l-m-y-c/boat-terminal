import 'dart:async';
import 'dart:convert';

import 'package:app_links/app_links.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:wakelock_plus/wakelock_plus.dart';

import 'app_settings.dart';
import 'boat_setup.dart';
import 'boat_wifi.dart';
import 'diagnostics.dart';
import 'event_log.dart';
import 'home_flow.dart';
import 'instruments.dart';
import 'journey.dart';
import 'last_boat.dart';
import 'network_page.dart';
import 'nmea_data_page.dart';
import 'tab_pager.dart';
import 'nmea_console.dart';
import 'pairing_link.dart';
import 'protocol.dart';
import 'qr_scan_page.dart';
import 'diag_bundle.dart';
import 'settings_page.dart';
import 'terminal_stats.dart';

import 'package:package_info_plus/package_info_plus.dart';

import 'dart:io' show Directory, File;

import 'ui_kit.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  EventLog.captureDebugPrint();
  await AppSettings.instance.load();
  runApp(const LmycApp());
}

ThemeData lmycTheme(Brightness brightness) {
  final dark = brightness == Brightness.dark;
  final pal = dark ? Palette.dark : Palette.light;
  return ThemeData(
    colorScheme: ColorScheme.fromSeed(
      seedColor: const Color(0xFF001F3F),
      brightness: brightness,
    ).copyWith(surface: pal.card),
    useMaterial3: true,
    scaffoldBackgroundColor: pal.page,
    appBarTheme: AppBarTheme(
      centerTitle: true,
      backgroundColor: dark ? const Color(0xFF07152A) : const Color(0xFF001F3F),
      foregroundColor: Colors.white,
    ),
  );
}

class LmycApp extends StatelessWidget {
  const LmycApp({super.key});

  @override
  Widget build(BuildContext context) {
    final settings = AppSettings.instance;
    return ListenableBuilder(
      listenable: settings,
      builder: (context, _) => MaterialApp(
        title: 'LMYC',
        debugShowCheckedModeBanner: false,
        theme: lmycTheme(Brightness.light),
        darkTheme: lmycTheme(Brightness.dark),
        themeMode: settings.themeMode,
        home: const HomePage(),
      ),
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

/// Five tab labels must fit a small phone at large text: the bar's labels
/// stop growing at 1.15x (the pages themselves still scale fully).
const kTabBarMaxTextScale = 1.15;

Widget tabBarTextScale(BuildContext context, Widget bar) {
  final mq = MediaQuery.of(context);
  return MediaQuery(
    data: mq.copyWith(
      textScaler: mq.textScaler.clamp(maxScaleFactor: kTabBarMaxTextScale),
    ),
    child: bar,
  );
}

/// The bottom bar, in OnBoardTab order.
const kOnBoardDestinations = [
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
  NavigationDestination(
    icon: Icon(Icons.hub_outlined),
    selectedIcon: Icon(Icons.hub),
    label: 'Network',
  ),
  NavigationDestination(
    icon: Icon(Icons.troubleshoot_outlined),
    selectedIcon: Icon(Icons.troubleshoot),
    label: 'Diagnostics',
  ),
];

enum OnBoardTab { boat, instruments, nmeaData, network, diagnostics }

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

  /// Display name from the terminal's link; null from older terminals.
  String? _boatName;
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
  String? _statsRaw;

  /// Collect diagnostics: manual, Mark problem, and automatic snapshots when
  /// the bus goes wrong (the terminal's rings are RAM-only).
  late final DiagBundleController _diag = DiagBundleController(
    dir: _diagDir,
    phoneContext: _diagPhoneContext,
  );

  bool _wifiReading = false;
  bool _wifiChecking = false;
  String? _wifiProblem;
  BoatWifiStatus? _wifiStatus;

  /// The newest check, including the relay's data age, which the equality
  /// on [BoatWifiStatus] ignores so the journey does not rebuild for it.
  BoatWifiStatus? _wifiLatest;
  Timer? _wifiTimer;

  /// When the open app first saw the relay off the boat; null while on it.
  DateTime? _offBoatSince;

  /// The relay's Wi-Fi request can stall (2026-10-28): after this long off
  /// the boat with the app open, it is asked to start a fresh request.
  static const _kOffBoatRestart = Duration(seconds: 25);

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
  Duration get _countdown => AppSettings.instance.countdown;

  /// Whether the screen is being held on for Instruments.
  bool _wakeHeld = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    AppSettings.instance.addListener(_settingsChanged);
    unawaited(_startup());
  }

  void _settingsChanged() {
    if (mounted) setState(() {});
  }

  /// Holds the screen on while Instruments shows, when the member asked.
  void _syncWakelock(bool want) {
    if (want == _wakeHeld) return;
    _wakeHeld = want;
    unawaited(WakelockPlus.toggle(enable: want).catchError((_) {}));
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
    _noteForLog();
    _ui.ping();
  }

  BlePhase? _loggedPhase;
  String? _loggedDetail;
  String? _loggedWifiProblem;

  /// Every change of Bluetooth step or message goes into Recent events.
  void _noteForLog() {
    final log = EventLog.instance;
    if (_blePhase != _loggedPhase) {
      _loggedPhase = _blePhase;
      log.add('Bluetooth: $_phaseLabel');
    }
    if (_bleDetail != _loggedDetail) {
      _loggedDetail = _bleDetail;
      if (_bleDetail.trim().isNotEmpty) log.add(_bleDetail);
    }
    if (_wifiProblem != _loggedWifiProblem) {
      _loggedWifiProblem = _wifiProblem;
      if (_wifiProblem != null) log.add('Wi-Fi: $_wifiProblem');
    }
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
      _boatName = link.boatName;
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

    // On a cold start the first state can be `unknown` before Android
    // reports; wait briefly for a real one so a saved boat is rejoined.
    final adapterState = await FlutterBluePlus.adapterState
        .firstWhere((s) => s != BluetoothAdapterState.unknown)
        .timeout(
          const Duration(seconds: 3),
          onTimeout: () => BluetoothAdapterState.unknown,
        );
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
    if (target != null && AppSettings.instance.autoJoin) {
      _startCountdown(target);
    }
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
      _boatName = null;
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
          _boatName = link?.boatName ?? _boatName;
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
  Future<void> _startRelay(BoatWifiInfo info, {bool force = false}) async {
    try {
      await Permission.notification.request();
      // A refused Connect dialog is not asked again by a running service.
      if (_wifiStatus?.relayDeclined == true) await BoatWifi.stopRelay();
      await BoatWifi.startRelay(
        ip: info.ip,
        ssid: info.ssid,
        psk: info.psk,
        force: force,
      );
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
      _wifiLatest = status;
      _ui.ping();
      // The relay came back without the Wi-Fi details: hand them over again.
      if (status.relayNeedsApp) unawaited(_startRelay(info));
      // Still not on the boat after a while with the app open: a pending
      // request may never finish, so ask for a fresh one (not after a "no").
      if (status.onBoat) {
        _offBoatSince = null;
      } else if (!status.relayDeclined) {
        final since = _offBoatSince ??= DateTime.now();
        if (DateTime.now().difference(since) >= _kOffBoatRestart) {
          _offBoatSince = DateTime.now();
          debugPrint(
            'boat wifi: $_kOffBoatRestart off the boat, fresh request',
          );
          unawaited(_startRelay(info, force: true));
        }
      }
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
      final raw = utf8.decode(await chr.read(), allowMalformed: true);
      final next = TerminalStats.parse(raw);
      if (next != null) {
        _statsRaw = raw.split('\u0000').first.trim();
        _diag.observe(next);
      }
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
    unawaited(HapticFeedback.mediumImpact());
    _tab = OnBoardTab.values.byName(AppSettings.instance.startTab.name);
    _splashTimer?.cancel();
    if (!AppSettings.instance.arrivalSplash) return;
    _splashing = true;
    _splashTimer = Timer(kArrivalSplash, () {
      if (mounted) setState(() => _splashing = false);
    });
  }

  @override
  void dispose() {
    _diag.dispose();
    AppSettings.instance.removeListener(_settingsChanged);
    _syncWakelock(false);
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

  void _openSettings() {
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (routeContext) => ListenableBuilder(
          listenable: _ui,
          builder: (_, _) => SettingsPage(
            settings: AppSettings.instance,
            savedBoat: _lastBoat == null ? null : _savedBoatLabel,
            onForget: _forgetBoat,
            terminalId: _terminalId,
            onOpenDiagnostics: _openStatus,
          ),
        ),
      ),
    );
  }

  void _openStatus() {
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (routeContext) => Scaffold(
          appBar: AppBar(title: const Text('Diagnostics')),
          body: ListenableBuilder(
            listenable: _ui,
            builder: (_, _) => _diagnosticsView(routeContext),
          ),
        ),
      ),
    );
  }

  DiagnosticsSnapshot _diagnosticsSnapshot() => DiagnosticsSnapshot(
    takenAt: DateTime.now(),
    boat: _boatTitle,
    terminalId: _terminalId,
    bleName: _bleName,
    bleState: switch (_blePhase) {
      BlePhase.paired => BleState.paired,
      BlePhase.failed => BleState.failed,
      BlePhase.idle => BleState.idle,
      _ => BleState.working,
    },
    bleLabel: _blePhase == BlePhase.paired ? 'Connected' : _phaseLabel,
    bleDetail: _bleDetail,
    rssi: _rssi,
    sessionReply: _sessionReply,
    ssid: BoatSession.info?.ssid,
    ip: BoatSession.info?.ip,
    wifiReading: _wifiReading,
    wifiProblem: _wifiProblem,
    wifi: _wifiLatest ?? _wifiStatus,
    stats: _stats,
    statsAt: _statsAt,
  );

  Future<Directory?> _diagDir() async {
    try {
      final p = await BoatWifi.filesDir();
      return p == null ? null : Directory('$p/$kDiagFolder');
    } catch (_) {
      return null;
    }
  }

  /// The phone half of a diagnostics bundle.
  Future<(Map<String, Object?>, Map<String, String>)>
  _diagPhoneContext() async {
    final phone = <String, Object?>{'boat': _boatTitle};
    final files = <String, String>{};
    try {
      final pi = await PackageInfo.fromPlatform();
      phone['app'] = '${pi.version}+${pi.buildNumber}';
      phone['package'] = pi.packageName;
    } catch (_) {}
    final info = BoatSession.info;
    try {
      final w = await BoatWifi.statusMap(ip: info?.ip ?? kSoftApIp);
      phone['wifi_relay'] = w;
      files['phone/wifi-status.json'] = const JsonEncoder.withIndent('  ')
          .convert(w);
    } catch (e) {
      phone['wifi_relay_error'] = '$e';
    }
    final stats = _stats;
    if (stats != null) {
      phone['chart_apps_on_terminal'] = stats.chartApps;
      phone['sentences_out'] = stats.sentencesOut;
    }
    if (_statsRaw != null) files['phone/ble-stats.json'] = _statsRaw!;
    try {
      final dir = await BoatWifi.filesDir();
      for (final n in ['relay-log.1.txt', 'relay-log.txt']) {
        final f = File('$dir/$n');
        if (dir != null && f.existsSync()) {
          files['phone/$n'] = await f.readAsString();
        }
      }
    } catch (_) {}
    files['phone/report.txt'] = diagnosticsReport(
      _diagnosticsSnapshot(),
      events: EventLog.instance.events,
    );
    return (phone, files);
  }

  Widget _diagnosticsView(BuildContext pageContext) {
    final device = _device;
    final canReconnect =
        _blePhase != BlePhase.paired && !_busy && _bleName != null;
    return DiagnosticsView(
      snapshot: _diagnosticsSnapshot(),
      hubFetch: readHubStatus,
      diag: _diag,
      onReconnect: canReconnect ? _retry : null,
      onSentences: device == null
          ? null
          : () => Navigator.of(pageContext).push(
              MaterialPageRoute<void>(
                builder: (_) =>
                    NmeaConsolePage(device: device, terminalIp: kRelayHost),
              ),
            ),
      onForget: _lastBoat == null ? null : _forgetBoat,
      forgetLabel: _lastBoat == null ? null : _savedBoatLabel,
    );
  }

  /// Stop rejoining the saved boat on launch. The current link stays up.
  Future<void> _forgetBoat() async {
    await LastBoat.forget();
    EventLog.instance.add('Forgot the saved boat');
    if (mounted) setState(() => _lastBoat = null);
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

  /// The saved boat as crew say it: the terminal's display name while it is
  /// known, else the title parsed from the saved Bluetooth name.
  String get _savedBoatLabel {
    final display = _boatName;
    if (display != null && display.isNotEmpty) return display;
    return TerminalName.parse(_lastBoat!).title;
  }

  String get _boatTitle {
    final display = _boatName;
    if (display != null && display.isNotEmpty) return display;
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
    _syncWakelock(
      onBoard &&
          _tab == OnBoardTab.instruments &&
          AppSettings.instance.keepScreenOn,
    );
    final contentKey = stop == JourneyStop.arrived
        ? (_splashing ? 'splash' : 'tabs')
        : '$stop/${_landing.name}/${_wifiSetup.name}';
    return Scaffold(
      backgroundColor: Palette.of(context).page,
      appBar: AppBar(
        title: BoatTitle(
          name: _boatTitle,
          simulated: _stats?.simulated ?? false,
        ),
        actions: [
          IconButton(
            tooltip: 'Settings',
            onPressed: _openSettings,
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
          ? tabBarTextScale(
              context,
              NavigationBar(
                selectedIndex: _tab.index,
                onDestinationSelected: (i) => _openTab(OnBoardTab.values[i]),
                destinations: kOnBoardDestinations,
              ),
            )
          : null,
    );
  }

  Widget _onBoardTab() => WrapTabPager(
    count: OnBoardTab.values.length,
    index: _tab.index,
    onChanged: (i) => _openTab(OnBoardTab.values[i]),
    builder: (context, i) => _tabPage(OnBoardTab.values[i]),
  );

  Widget _tabPage(OnBoardTab tab) => switch (tab) {
    OnBoardTab.boat => _boatTab(context),
    OnBoardTab.instruments => const InstrumentsView(),
    OnBoardTab.diagnostics => ListenableBuilder(
      listenable: _ui,
      builder: (context, _) => _diagnosticsView(context),
    ),
    OnBoardTab.network => ListView(
      padding: const EdgeInsets.fromLTRB(0, 12, 0, 32),
      children: const [NetworkView()],
    ),
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
              style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                fontWeight: FontWeight.w700,
                color: Palette.of(context).ink,
              ),
            ),
            const SizedBox(height: 8),
            Text(
              'The closest is usually the boat you are on.',
              textAlign: TextAlign.center,
              style: TextStyle(color: Palette.of(context).muted, height: 1.4),
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
            _pasteCodeButton(),
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
            _pasteCodeButton(),
            _diagnosticsLink(),
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
          _diagnosticsLink(),
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
      secondary: [if (setup == WifiSetup.problem) _diagnosticsLink()],
    );
  }

  /// On a step that failed: the full picture, as a page over the journey.
  Widget _diagnosticsLink() => TextButton.icon(
    key: const ValueKey('diagnostics-link'),
    onPressed: _openStatus,
    icon: const Icon(Icons.troubleshoot, size: 18),
    label: const Text('Diagnostics'),
  );

  /// Join from an lmyc:// link copied out of a message or the terminal's
  /// web page, when the QR cannot be scanned.
  Widget _pasteCodeButton() => TextButton.icon(
    key: const ValueKey('paste-code'),
    onPressed: _busy ? null : () => unawaited(_pasteCodeSheet()),
    icon: const Icon(Icons.content_paste, size: 18),
    label: const Text('Paste a pairing link'),
  );

  Future<void> _pasteCodeSheet() async {
    final apply = await showModalBottomSheet<bool>(
      context: context,
      isScrollControlled: true,
      showDragHandle: true,
      builder: (sheet) => Padding(
        padding: EdgeInsets.fromLTRB(
          24,
          0,
          24,
          24 + MediaQuery.of(sheet).viewInsets.bottom,
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Text(
              'Paste a pairing link',
              style: Theme.of(sheet).textTheme.titleLarge
                  ?.copyWith(fontWeight: FontWeight.w700),
            ),
            const SizedBox(height: 8),
            const Text(
              'The lmyc:// link the terminal\'s QR code holds. Use it when '
              'the camera cannot read the QR.',
            ),
            const SizedBox(height: 14),
            TextField(
              controller: _pasteController,
              autofocus: true,
              decoration: const InputDecoration(
                labelText: 'lmyc://…',
                border: OutlineInputBorder(),
              ),
              minLines: 1,
              maxLines: 3,
              onSubmitted: (_) => Navigator.of(sheet).pop(true),
            ),
            const SizedBox(height: 14),
            FilledButton(
              onPressed: () => Navigator.of(sheet).pop(true),
              child: const Text('Join'),
            ),
          ],
        ),
      ),
    );
    if (apply == true && mounted) _applyPastedUri();
  }

  /// The moment of arrival: a big tick and the boat's name, then the tabs.
  /// Tap to skip.
  Widget _arrivalSplash(BuildContext context) {
    return GestureDetector(
      key: const ValueKey('arrival-splash'),
      behavior: HitTestBehavior.opaque,
      onTap: () => _openTab(
        OnBoardTab.values.byName(AppSettings.instance.startTab.name),
      ),
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
              style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                fontWeight: FontWeight.w700,
                color: Palette.of(context).ink,
              ),
            ),
            const SizedBox(height: 8),
            Text(
              'Chart apps on this phone can get the boat\'s data.',
              textAlign: TextAlign.center,
              style: TextStyle(color: Palette.of(context).muted, fontSize: 16),
            ),
          ],
        ),
      ),
    );
  }

  Widget _boatTab(BuildContext context) => BoatTabView(
    state: _stats?.state,
    linked: _blePhase == BlePhase.paired,
    onReconnect: _busy ? null : _retry,
    onChartApp: _openNmea,
    onLeave: () => unawaited(_leaveBoat()),
  );

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
    setState(() {
      _wifiStatus = null;
      _wifiLatest = null;
    });
    await _disconnect();
  }
}

/// A short line under the arrived header, tinted by what it is about.
class NoticeBox extends StatelessWidget {
  const NoticeBox({
    super.key,
    required this.color,
    required this.text,
    this.action,
  });

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

/// The Boat tab: problems worth knowing about, then what to do on this
/// phone. No status panel; the data source lives on NMEA data and in the
/// title badge.
class BoatTabView extends StatelessWidget {
  const BoatTabView({
    super.key,
    required this.state,
    required this.linked,
    required this.onReconnect,
    required this.onChartApp,
    required this.onLeave,
  });

  final BusState? state;
  final bool linked;
  final VoidCallback? onReconnect;
  final VoidCallback onChartApp;
  final VoidCallback onLeave;

  @override
  Widget build(BuildContext context) {
    final quiet = state == BusState.waiting || state == BusState.quiet;
    final fault = state == BusState.busOff || state == BusState.failed;
    return ListView(
      key: const ValueKey('boat-tab'),
      padding: const EdgeInsets.fromLTRB(20, 18, 20, 24),
      children: [
        if (fault || quiet) ...[
          NoticeBox(
            color: fault ? kRed : Palette.of(context).muted,
            text: fault
                ? 'Connected, but the boat\'s NMEA 2000 bus has a fault.'
                : 'Connected. Waiting for data from the boat\'s instruments.',
          ),
          const SizedBox(height: 12),
        ],
        if (!linked) ...[
          NoticeBox(
            color: Palette.of(context).muted,
            text: 'Bluetooth to the terminal dropped. Chart apps keep working.',
            action: TextButton(
              onPressed: onReconnect,
              child: const Text('Reconnect'),
            ),
          ),
          const SizedBox(height: 12),
        ],
        const SectionLabel('On this phone'),
        const SizedBox(height: 10),
        OptionTile(
          icon: Icons.map_outlined,
          title: 'Connect a chart app',
          subtitle: 'Navionics or i-Boating: UDP, port 10110',
          onTap: onChartApp,
        ),
        OptionTile(
          icon: Icons.logout,
          title: 'Leave the boat',
          subtitle: 'Stop sharing the boat data on this phone',
          onTap: onLeave,
        ),
      ],
    );
  }
}
