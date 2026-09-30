import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:geolocator/geolocator.dart';
import 'package:http/http.dart' as http;
import 'package:image/image.dart' as img;
import 'package:image_picker/image_picker.dart';

/// Wi-Fi data plane: Navionics TCP 10110, chart JPEG snapshot, optional GNSS.
class BoatLinkPage extends StatefulWidget {
  const BoatLinkPage({
    super.key,
    this.suggestedIp,
  });

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

  @override
  void initState() {
    super.initState();
    _ipController.text = widget.suggestedIp?.isNotEmpty == true
        ? widget.suggestedIp!
        : '192.168.4.1';
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
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Copied $text')),
      );
    }
  }

  Future<void> _sendSnapshot() async {
    final picked = await _picker.pickImage(source: ImageSource.gallery);
    if (picked == null) return;
    setState(() {
      _busy = true;
      _status = 'Resizing to 800×480 JPEG…';
    });
    try {
      final bytes = await File(picked.path).readAsBytes();
      final decoded = img.decodeImage(bytes);
      if (decoded == null) {
        throw Exception('Could not decode image');
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
      final jpeg = img.encodeJpg(framed, quality: 72);
      if (jpeg.length > 220 * 1024) {
        throw Exception('JPEG ${jpeg.length} bytes — pick a simpler image');
      }
      final uri = Uri.parse('http://$_host/snapshot');
      final resp = await http
          .post(
            uri,
            headers: {'Content-Type': 'image/jpeg'},
            body: jpeg,
          )
          .timeout(const Duration(seconds: 20));
      setState(() => _status = resp.statusCode == 200
          ? 'Snapshot sent (${jpeg.length} B). Tap Chart on the 7″.'
          : 'HTTP ${resp.statusCode}: ${resp.body}');
    } catch (e) {
      setState(() => _status = 'Snapshot failed: $e');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  String _nmeaChecksum(String body) {
    var cs = 0;
    for (final c in body.codeUnits) {
      cs ^= c;
    }
    return cs.toRadixString(16).toUpperCase().padLeft(2, '0');
  }

  String _fmtLat(double lat) {
    final hemi = lat >= 0 ? 'N' : 'S';
    final a = lat.abs();
    final deg = a.floor();
    final min = (a - deg) * 60.0;
    return '${deg.toString().padLeft(2, '0')}${min.toStringAsFixed(4).padLeft(7, '0')},$hemi';
  }

  String _fmtLon(double lon) {
    final hemi = lon >= 0 ? 'E' : 'W';
    final a = lon.abs();
    final deg = a.floor();
    final min = (a - deg) * 60.0;
    return '${deg.toString().padLeft(3, '0')}${min.toStringAsFixed(4).padLeft(7, '0')},$hemi';
  }

  Future<void> _pushGnssOnce() async {
    final perm = await Geolocator.requestPermission();
    if (perm == LocationPermission.denied ||
        perm == LocationPermission.deniedForever) {
      setState(() => _status = 'Location permission denied');
      return;
    }
    final pos = await Geolocator.getCurrentPosition();
    final now = DateTime.now().toUtc();
    final t =
        '${now.hour.toString().padLeft(2, '0')}${now.minute.toString().padLeft(2, '0')}${now.second.toString().padLeft(2, '0')}.00';
    final d =
        '${now.day.toString().padLeft(2, '0')}${now.month.toString().padLeft(2, '0')}${now.year.toString().substring(2)}';
    final sog = pos.speed.isFinite ? (pos.speed * 1.94384) : 0.0;
    final cog = pos.heading.isFinite ? pos.heading : 0.0;
    final lat = _fmtLat(pos.latitude);
    final lon = _fmtLon(pos.longitude);
    final rmcBody =
        'GPRMC,$t,A,$lat,$lon,${sog.toStringAsFixed(1)},${cog.toStringAsFixed(1)},$d,,,A';
    final ggaBody =
        'GPGGA,$t,$lat,$lon,1,08,1.0,${pos.altitude.toStringAsFixed(1)},M,,,';
    final rmc = '\$$rmcBody*${_nmeaChecksum(rmcBody)}';
    final gga = '\$$ggaBody*${_nmeaChecksum(ggaBody)}';
    final uri = Uri.parse('http://$_host/inject');
    final resp = await http
        .post(uri, body: '$rmc\n$gga\n')
        .timeout(const Duration(seconds: 5));
    setState(() =>
        _status = 'GNSS ${resp.statusCode}: ${resp.body.trim()}  SOG ${sog.toStringAsFixed(1)} kn');
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
    await _pushGnssOnce();
    _gnssTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      _pushGnssOnce();
    });
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
            decoration: const InputDecoration(
              labelText: 'Terminal IP',
              helperText: 'SoftAP is 192.168.4.1  ·  STA IP is on the helm LINK card',
              border: OutlineInputBorder(),
            ),
          ),
          const SizedBox(height: 16),
          const Text('Navionics', style: TextStyle(fontWeight: FontWeight.w600)),
          const SizedBox(height: 8),
          _copyRow('SSID', 'LMYC-BENCH-01'),
          _copyRow('TCP', '192.168.4.1:10110'),
          _copyRow('UDP', '10110 or 2000'),
          const SizedBox(height: 8),
          const Text(
            'Navionics → Menu → Connected devices → Add a device → '
            'TCP 192.168.4.1 port 10110. Password is AP_PASSWORD from secrets.h '
            '(default lmycboat).',
            style: TextStyle(color: Colors.black54, height: 1.35),
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
            label: Text(_gnssOn ? 'Stop phone GNSS' : 'Push phone GNSS (LOCAL)'),
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
