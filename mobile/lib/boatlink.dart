import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart' show compute;
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:geolocator/geolocator.dart';
import 'package:http/http.dart' as http;
import 'package:image/image.dart' as img;
import 'package:image_picker/image_picker.dart';

import 'boat_wifi.dart';
import 'nmea_sentences.dart';
import 'protocol.dart';

/// Chart posts the terminal accepts (firmware `kSnapshotJpegMax`).
const int kSnapshotJpegMax = 220 * 1024;

/// Letterbox [bytes] onto the 800×480 panel and encode a JPEG.
/// Top level so it can run in a background isolate.
Uint8List frameChartSnapshot(Uint8List bytes) {
  final decoded = img.decodeImage(bytes);
  if (decoded == null) {
    throw const FormatException('Could not decode image');
  }
  final framed = img.Image(width: 800, height: 480);
  img.fill(framed, color: img.ColorRgb8(7, 20, 34));
  final fitted = img.copyResize(
    decoded,
    width: 800,
    height: 480,
    maintainAspect: true,
    interpolation: img.Interpolation.linear,
  );
  final ox = ((800 - fitted.width) / 2).round();
  final oy = ((480 - fitted.height) / 2).round();
  img.compositeImage(framed, fitted, dstX: ox, dstY: oy);
  return img.encodeJpg(framed, quality: 72);
}

/// Wi-Fi data plane: Navionics TCP 10110, chart JPEG snapshot, optional GNSS.
class BoatLinkPage extends StatefulWidget {
  const BoatLinkPage({super.key, this.suggestedIp});

  final String? suggestedIp;

  @override
  State<BoatLinkPage> createState() => _BoatLinkPageState();
}

class _BoatLinkPageState extends State<BoatLinkPage> {
  final _ipController = TextEditingController();
  final _picker = ImagePicker();
  bool _busy = false;
  String _status = '';
  Timer? _gnssTimer;
  bool _gnssOn = false;
  bool _gnssInFlight = false;

  @override
  void initState() {
    super.initState();
    _ipController.text = widget.suggestedIp?.isNotEmpty == true
        ? widget.suggestedIp!
        : kSoftApIp;
  }

  @override
  void dispose() {
    _gnssTimer?.cancel();
    _ipController.dispose();
    super.dispose();
  }

  String get _host => _ipController.text.trim();

  Future<void> _copy(String text) async {
    await Clipboard.setData(ClipboardData(text: text));
    if (mounted) {
      ScaffoldMessenger.of(context)
          .showSnackBar(SnackBar(content: Text('Copied $text')));
    }
  }

  Future<void> _sendSnapshot() async {
    final picked = await _picker.pickImage(source: ImageSource.gallery);
    if (picked == null || !mounted) return;
    setState(() {
      _busy = true;
      _status = 'Resizing to 800×480 JPEG…';
    });
    try {
      final bytes = await File(picked.path).readAsBytes();
      final jpeg = await compute(frameChartSnapshot, bytes);
      if (jpeg.length > kSnapshotJpegMax) {
        throw Exception('JPEG ${jpeg.length} bytes — pick a simpler image');
      }
      final uri = Uri.parse('http://$_host/snapshot');
      final resp = await http
          .post(uri, headers: {'Content-Type': 'image/jpeg'}, body: jpeg)
          .timeout(const Duration(seconds: 20));
      if (!mounted) return;
      setState(
        () => _status = resp.statusCode == 200
            ? 'Snapshot sent (${jpeg.length} B). Tap Chart on the 7″.'
            : 'HTTP ${resp.statusCode}: ${resp.body}',
      );
    } catch (e) {
      if (!mounted) return;
      setState(() => _status = 'Snapshot failed: $e');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  /// One RMC + GGA post. Returns a status line; never throws.
  Future<String> _pushGnssOnce() async {
    try {
      final perm = await Geolocator.requestPermission();
      if (perm == LocationPermission.denied ||
          perm == LocationPermission.deniedForever) {
        return 'Location permission denied';
      }
      final pos = await Geolocator.getCurrentPosition().timeout(
        const Duration(seconds: 5),
      );
      final now = DateTime.now().toUtc();
      final sog = pos.speed * 1.94384;
      final rmc = buildRmc(
        utc: now,
        lat: pos.latitude,
        lon: pos.longitude,
        sogKn: sog,
        cogDeg: pos.heading,
      );
      final gga = buildGga(
        utc: now,
        lat: pos.latitude,
        lon: pos.longitude,
        altitudeM: pos.altitude,
      );
      final resp = await http
          .post(Uri.parse('http://$_host/inject'), body: '$rmc\n$gga\n')
          .timeout(const Duration(seconds: 5));
      final shownSog = sog.isFinite && sog >= 0 ? sog : 0.0;
      return 'GNSS ${resp.statusCode}: ${resp.body.trim()}  '
          'SOG ${shownSog.toStringAsFixed(1)} kn';
    } catch (e) {
      return 'GNSS push failed: $e';
    }
  }

  /// Timer tick. Skips a tick while the last push is still out, so a slow
  /// fix or a dead link cannot pile up requests.
  Future<void> _gnssTick() async {
    if (_gnssInFlight) return;
    _gnssInFlight = true;
    final status = await _pushGnssOnce();
    _gnssInFlight = false;
    if (mounted && _gnssOn) setState(() => _status = status);
  }

  Future<void> _toggleGnss() async {
    if (_gnssOn) {
      _gnssTimer?.cancel();
      setState(() {
        _gnssOn = false;
        _status = 'Phone GNSS stopped';
      });
      return;
    }
    setState(() => _gnssOn = true);
    _gnssTimer = Timer.periodic(const Duration(seconds: 1), (_) => _gnssTick());
    await _gnssTick();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('BoatLink / Navionics')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          const Text(
            'Join the boat Wi-Fi, then point Navionics at the gateway. '
            'Screenshot a chart in Navionics and send it to the 7″.',
            style: TextStyle(height: 1.4),
          ),
          const SizedBox(height: 16),
          TextField(
            controller: _ipController,
            // The Navionics rows below show this address.
            onChanged: (_) => setState(() {}),
            decoration: const InputDecoration(
              labelText: 'Terminal IP',
              helperText:
                  'SoftAP is $kSoftApIp  ·  STA IP is on the helm LINK card',
              border: OutlineInputBorder(),
            ),
          ),
          const SizedBox(height: 16),
          const Text(
            'Navionics',
            style: TextStyle(fontWeight: FontWeight.w600),
          ),
          const SizedBox(height: 8),
          _copyRow(
            'SSID',
            BoatSession.ssid.isNotEmpty ? BoatSession.ssid : 'LMYC-<boat id>',
          ),
          _copyRow('TCP', '$_host:$kGatewayTcpPort'),
          _copyRow('UDP', '$kGatewayUdpPort or $kGatewayUdpAltPort'),
          const SizedBox(height: 8),
          Text(
            'Navionics → Menu → Connected devices → Add a device → '
            'TCP $_host port $kGatewayTcpPort. Password is AP_PASSWORD from secrets.h '
            '(default lmycboat).',
            style: const TextStyle(color: Colors.black54, height: 1.35),
          ),
          const SizedBox(height: 20),
          FilledButton.icon(
            onPressed: _busy ? null : _sendSnapshot,
            icon: const Icon(Icons.photo_library_outlined),
            label: const Text('Send chart snapshot'),
            style: FilledButton.styleFrom(
              backgroundColor: const Color(0xFF00B4A6),
              padding: const EdgeInsets.symmetric(vertical: 14),
            ),
          ),
          const SizedBox(height: 8),
          OutlinedButton.icon(
            onPressed: _busy ? null : _toggleGnss,
            icon: Icon(_gnssOn ? Icons.gps_fixed : Icons.gps_off),
            label: Text(
              _gnssOn ? 'Stop phone GNSS' : 'Push phone GNSS (LOCAL)',
            ),
          ),
          const SizedBox(height: 8),
          const Text(
            'GNSS inject only works after you tap GNSS LOCAL on the 7″. '
            'It never talks on RS485.',
            style: TextStyle(color: Colors.black54, fontSize: 12),
          ),
          if (_status.isNotEmpty) ...[
            const SizedBox(height: 16),
            Text(_status, style: const TextStyle(height: 1.35)),
          ],
        ],
      ),
    );
  }

  Widget _copyRow(String k, String v) {
    return ListTile(
      dense: true,
      contentPadding: EdgeInsets.zero,
      title: Text(k, style: const TextStyle(color: Colors.black54)),
      subtitle: Text(v, style: const TextStyle(fontFamily: 'monospace')),
      trailing: IconButton(
        icon: const Icon(Icons.copy, size: 18),
        onPressed: () => _copy(v),
      ),
    );
  }
}
