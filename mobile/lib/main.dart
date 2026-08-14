import 'dart:async';

import 'package:app_links/app_links.dart';
import 'package:flutter/material.dart';

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
          seedColor: const Color(0xFF001F3F), // Navy — matches terminal branding
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

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final _appLinks = AppLinks();
  StreamSubscription<Uri>? _linkSubscription;

  String? _lastDeepLink;
  String? _boatId;
  String? _terminalId;
  String? _bleName;
  String? _oob;
  String? _version;

  @override
  void initState() {
    super.initState();
    _initDeepLinks();
  }

  Future<void> _initDeepLinks() async {
    // Handle link that opened the app (cold start)
    try {
      final initialUri = await _appLinks.getInitialLink();
      if (initialUri != null) {
        _handleIncomingLink(initialUri);
      }
    } catch (e) {
      debugPrint('Failed to get initial link: $e');
    }

    // Handle links while the app is already running
    _linkSubscription = _appLinks.uriLinkStream.listen(
      (uri) => _handleIncomingLink(uri),
      onError: (err) => debugPrint('Deep link error: $err'),
    );
  }

  void _handleIncomingLink(Uri uri) {
    debugPrint('Incoming deep link: $uri');

    if (uri.scheme != 'lmyc') return;

    // Real terminal format (from firmware):
    //   lmyc://pair?v=1&boat=BENCH-01&tid=WS7-001&ble=LMYC-D649&oob=226c6c38...
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
          content: Text(
            'Pairing data received for ${_boatId ?? "unknown boat"}',
          ),
          backgroundColor: const Color(0xFF00B4A6),
        ),
      );
    }
  }

  @override
  void dispose() {
    _linkSubscription?.cancel();
    super.dispose();
  }

  String _short(String? value, [int max = 16]) {
    if (value == null || value.isEmpty) return '—';
    if (value.length <= max) return value;
    return '${value.substring(0, max)}…';
  }

  @override
  Widget build(BuildContext context) {
    final hasPairingData = _oob != null;

    return Scaffold(
      appBar: AppBar(
        title: const Text('LMYC Boat Terminal'),
      ),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const SizedBox(height: 16),
              const Icon(
                Icons.sailing,
                size: 72,
                color: Color(0xFF001F3F),
              ),
              const SizedBox(height: 16),
              Text(
                'Lower Mainland Yacht Club',
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                      fontWeight: FontWeight.bold,
                      color: const Color(0xFF001F3F),
                    ),
              ),
              const SizedBox(height: 8),
              Text(
                'Companion app for boat check-in and terminal pairing',
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                      color: Colors.black54,
                    ),
              ),
              const SizedBox(height: 32),

              // Status card
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
                            'Deep-link status',
                            style: Theme.of(context).textTheme.titleMedium?.copyWith(
                                  fontWeight: FontWeight.w600,
                                ),
                          ),
                          const Spacer(),
                          if (hasPairingData)
                            Container(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 8,
                                vertical: 4,
                              ),
                              decoration: BoxDecoration(
                                color: const Color(0xFF00B4A6).withValues(alpha: 0.15),
                                borderRadius: BorderRadius.circular(12),
                              ),
                              child: const Text(
                                'Ready to pair',
                                style: TextStyle(
                                  color: Color(0xFF00B4A6),
                                  fontWeight: FontWeight.w600,
                                  fontSize: 12,
                                ),
                              ),
                            ),
                        ],
                      ),
                      const SizedBox(height: 16),
                      _statusRow('Boat', _boatId),
                      _statusRow('Terminal', _terminalId),
                      _statusRow('BLE name', _bleName),
                      _statusRow('OOB data', _short(_oob, 20)),
                      _statusRow('Version', _version),
                      if (_lastDeepLink != null) ...[
                        const SizedBox(height: 8),
                        const Divider(),
                        const SizedBox(height: 8),
                        _statusRow('Raw link', _short(_lastDeepLink, 40)),
                      ],
                    ],
                  ),
                ),
              ),

              const Spacer(),

              Text(
                hasPairingData
                    ? 'Pairing data received from terminal.\nNext step: BLE handshake.'
                    : 'Scan the QR code shown on the boat terminal\nto open lmyc:// and begin pairing.',
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      color: Colors.black45,
                    ),
              ),
              const SizedBox(height: 16),
            ],
          ),
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
