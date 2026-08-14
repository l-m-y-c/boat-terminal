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
  String? _pairingToken;
  String? _boatId;

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

    setState(() {
      _lastDeepLink = uri.toString();

      // Expected forms (examples):
      //   lmyc://pair?token=abc123&boat=deserata
      //   lmyc://boat/deserata
      if (uri.host == 'pair' || uri.pathSegments.contains('pair')) {
        _pairingToken = uri.queryParameters['token'];
        _boatId = uri.queryParameters['boat'];
      } else if (uri.host.isNotEmpty) {
        // lmyc://boatname or lmyc://boat/boatname
        _boatId = uri.host;
        if (uri.pathSegments.isNotEmpty) {
          _boatId = uri.pathSegments.last;
        }
      }
    });

    if (_pairingToken != null && mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Pairing token received for boat: ${_boatId ?? "unknown"}'),
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

  @override
  Widget build(BuildContext context) {
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
              const SizedBox(height: 40),

              // Status card
              Card(
                elevation: 2,
                child: Padding(
                  padding: const EdgeInsets.all(20),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'Deep-link status',
                        style: Theme.of(context).textTheme.titleMedium?.copyWith(
                              fontWeight: FontWeight.w600,
                            ),
                      ),
                      const SizedBox(height: 12),
                      _statusRow('Last link', _lastDeepLink ?? '— none yet —'),
                      const SizedBox(height: 8),
                      _statusRow('Boat', _boatId ?? '—'),
                      const SizedBox(height: 8),
                      _statusRow(
                        'Pairing token',
                        _pairingToken != null
                            ? '${_pairingToken!.substring(0, _pairingToken!.length.clamp(0, 12))}…'
                            : '—',
                      ),
                    ],
                  ),
                ),
              ),

              const Spacer(),

              Text(
                'Scan the QR code shown on the boat terminal\nto open lmyc:// and begin pairing.',
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

  Widget _statusRow(String label, String value) {
    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SizedBox(
          width: 110,
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
            value,
            style: const TextStyle(fontFamily: 'monospace'),
          ),
        ),
      ],
    );
  }
}
