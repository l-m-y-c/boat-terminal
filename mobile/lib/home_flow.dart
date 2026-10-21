import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'boat_wifi.dart';
import 'maintenance_book.dart';

/// One large choice on the connected home screen.
class HomeChoice extends StatelessWidget {
  const HomeChoice({
    super.key,
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.onTap,
  });

  final IconData icon;
  final String title;
  final String subtitle;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 12),
      child: Material(
        color: Colors.white,
        elevation: 0,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(16),
          side: const BorderSide(color: Color(0xFFE2E8F0)),
        ),
        child: InkWell(
          borderRadius: BorderRadius.circular(16),
          onTap: onTap,
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 18),
            child: Row(
              children: [
                Icon(icon, color: const Color(0xFF001F3F), size: 28),
                const SizedBox(width: 16),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        title,
                        style: const TextStyle(
                          fontSize: 17,
                          fontWeight: FontWeight.w600,
                          color: Color(0xFF001F3F),
                        ),
                      ),
                      const SizedBox(height: 2),
                      Text(
                        subtitle,
                        style: const TextStyle(
                          color: Colors.black54,
                          height: 1.3,
                        ),
                      ),
                    ],
                  ),
                ),
                const Icon(Icons.chevron_right, color: Colors.black38),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// Wi-Fi name, address, and the ports chart apps use.
class NmeaInfoPage extends StatelessWidget {
  const NmeaInfoPage({
    super.key,
    required this.ssid,
    required this.ip,
    required this.onBoat,
    this.simulated = false,
  });

  final String ssid;
  final String ip;
  final bool onBoat;

  /// The terminal is on its simulator, so chart apps get made-up data.
  final bool simulated;

  @override
  Widget build(BuildContext context) {
    final network = ssid.isEmpty ? 'Not received yet' : ssid;
    return Scaffold(
      appBar: AppBar(title: const Text('Chart apps')),
      body: ListView(
        padding: const EdgeInsets.all(24),
        children: [
          if (simulated) ...[
            Container(
              padding: const EdgeInsets.all(14),
              decoration: BoxDecoration(
                color: const Color(0xFFB7791F).withValues(alpha: 0.1),
                borderRadius: BorderRadius.circular(14),
                border: Border.all(
                  color: const Color(0xFFB7791F).withValues(alpha: 0.4),
                ),
              ),
              child: const Text(
                'The terminal is simulating a boat. Chart apps will show '
                'simulated position, depth and wind, not the boat\'s.',
                style: TextStyle(height: 1.4, color: Color(0xFF7A4F0E)),
              ),
            ),
            const SizedBox(height: 20),
          ],
          Text(
            onBoat
                ? 'This phone is on the boat Wi-Fi.'
                : 'Join the boat Wi-Fi before a chart app can see the stream.',
            style: const TextStyle(height: 1.4, fontSize: 16),
          ),
          const SizedBox(height: 20),
          _fact(context, 'Wi-Fi', network, network),
          _fact(context, 'Address', ip, ip),
          _fact(context, 'TCP', '$ip port 10110', '$ip:10110'),
          _fact(context, 'UDP', 'ports 10110 and 2000', '10110'),
          const SizedBox(height: 8),
          const Text(
            'In Navionics or i-Boating, add a TCP device at this address and port. '
            'UDP 2000 is the same stream.',
            style: TextStyle(color: Colors.black54, height: 1.4),
          ),
        ],
      ),
    );
  }

  Widget _fact(BuildContext context, String label, String value, String copy) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 14),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 72,
            child: Text(
              label,
              style: const TextStyle(color: Colors.black54, height: 1.4),
            ),
          ),
          Expanded(
            child: Text(
              value,
              style: const TextStyle(
                fontFamily: 'monospace',
                fontSize: 16,
                height: 1.4,
              ),
            ),
          ),
          IconButton(
            tooltip: 'Copy',
            onPressed: () async {
              await Clipboard.setData(ClipboardData(text: copy));
              if (context.mounted) {
                ScaffoldMessenger.of(context)
                    .showSnackBar(SnackBar(content: Text('Copied $copy')));
              }
            },
            icon: const Icon(Icons.copy, size: 18),
          ),
        ],
      ),
    );
  }
}

class MaintenanceMenuPage extends StatelessWidget {
  const MaintenanceMenuPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Maintenance')),
      body: ListView(
        padding: const EdgeInsets.symmetric(vertical: 8),
        children: [
          ListTile(
            contentPadding: const EdgeInsets.symmetric(
              horizontal: 24,
              vertical: 8,
            ),
            leading: const Icon(
              Icons.report_outlined,
              color: Color(0xFF001F3F),
            ),
            title: const Text('Create a defect report'),
            subtitle: const Text('Photos and notes saved on the boat'),
            trailing: const Icon(Icons.chevron_right),
            onTap: () {
              Navigator.of(context).push(
                MaterialPageRoute<void>(
                  builder: (_) =>
                      MaintenanceBookPage(suggestedIp: BoatSession.ip),
                ),
              );
            },
          ),
          const Divider(height: 1),
          ListTile(
            contentPadding: const EdgeInsets.symmetric(
              horizontal: 24,
              vertical: 8,
            ),
            leading: const Icon(
              Icons.menu_book_outlined,
              color: Color(0xFF001F3F),
            ),
            title: const Text('Log entry'),
            subtitle: const Text('Not set up yet'),
            trailing: const Icon(Icons.chevron_right),
            onTap: () {
              Navigator.of(context).push(
                MaterialPageRoute<void>(builder: (_) => const LogEntryPage()),
              );
            },
          ),
        ],
      ),
    );
  }
}

class LogEntryPage extends StatelessWidget {
  const LogEntryPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Log entry')),
      body: const Padding(
        padding: EdgeInsets.all(24),
        child: Text(
          'Log entry is not set up yet.',
          style: TextStyle(fontSize: 16, height: 1.4),
        ),
      ),
    );
  }
}
