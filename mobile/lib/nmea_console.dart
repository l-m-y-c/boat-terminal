import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import 'nmea_sentences.dart';
import 'protocol.dart';

/// Live NMEA tap after BLE pair (GATT …000030 + optional TCP :10110).
class NmeaConsolePage extends StatefulWidget {
  const NmeaConsolePage({super.key, required this.device, this.terminalIp});

  final BluetoothDevice device;
  final String? terminalIp;

  static final Guid kLmycService = Guid(kLmycServiceUuid);
  static final Guid kLmycNmeaChr = Guid(kLmycNmeaUuid);

  @override
  State<NmeaConsolePage> createState() => _NmeaConsolePageState();
}

class _NmeaConsolePageState extends State<NmeaConsolePage> {
  final List<String> _lines = [];
  final TextEditingController _filterController = TextEditingController();
  final TextEditingController _ipController = TextEditingController();

  BluetoothCharacteristic? _nmeaChr;
  StreamSubscription<List<int>>? _notifySub;
  StreamSubscription<BluetoothConnectionState>? _connSub;
  Socket? _tcp;
  StreamSubscription<Uint8List>? _tcpSub;
  String _tcpAcc = '';

  bool _paused = false;
  bool _subscribed = false;
  String _filter = '';
  int _received = 0;
  int _badChecksum = 0;
  int _tcpLines = 0;
  DateTime? _windowStart;
  int _windowCount = 0;
  double _rateHz = 0;

  @override
  void initState() {
    super.initState();
    if (widget.terminalIp != null && widget.terminalIp!.isNotEmpty) {
      _ipController.text = widget.terminalIp!;
    }
    _connSub = widget.device.connectionState.listen((state) {
      if (state == BluetoothConnectionState.disconnected && mounted) {
        setState(() => _subscribed = false);
      }
    });
    unawaited(_startBle());
  }

  @override
  void dispose() {
    // Stop the terminal sending notifies nobody reads. Best effort: the link
    // may already be down.
    unawaited(_nmeaChr?.setNotifyValue(false).catchError((Object _) => false));
    unawaited(_notifySub?.cancel());
    unawaited(_connSub?.cancel());
    unawaited(_tcpSub?.cancel());
    _tcp?.destroy();
    _filterController.dispose();
    _ipController.dispose();
    super.dispose();
  }

  Future<void> _startBle() async {
    try {
      if (!widget.device.isConnected) {
        await widget.device.connect(
          timeout: const Duration(seconds: 15),
          autoConnect: false,
          mtu: null,
        );
      }
      // Prefer larger ATT MTU so full RMC/GGA fit on …000030. Ignore failure —
      // short sentences + TCP still work at MTU 23.
      try {
        await widget.device.requestMtu(185);
      } catch (e) {
        debugPrint('requestMtu: $e');
      }

      if (!mounted) return;
      final services = await widget.device.discoverServices();
      if (!mounted) return;
      BluetoothCharacteristic? nmea;
      for (final s in services) {
        if (s.uuid != NmeaConsolePage.kLmycService) continue;
        for (final c in s.characteristics) {
          if (c.uuid == NmeaConsolePage.kLmycNmeaChr) nmea = c;
        }
      }
      if (nmea == null) {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(
              content: Text(
                'No NMEA char …000030 — flash boatlink-v1 firmware',
              ),
            ),
          );
        }
        return;
      }

      await nmea.setNotifyValue(true);
      if (!mounted) {
        // Left the page while subscribing. dispose() never saw this char.
        await nmea.setNotifyValue(false).catchError((Object _) => false);
        return;
      }
      _nmeaChr = nmea;
      _notifySub = nmea.onValueReceived.listen(_onBleBytes);
      setState(() => _subscribed = true);
    } catch (e) {
      debugPrint('NMEA BLE subscribe failed: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('BLE NMEA subscribe failed: $e')),
        );
      }
    }
  }

  void _onBleBytes(List<int> bytes) {
    final text = _asciiLine(bytes);
    if (text.isEmpty) return;
    _ingest(text, fromTcp: false);
  }

  String _asciiLine(List<int> bytes) {
    final out = StringBuffer();
    for (final b in bytes) {
      if (b == 0) break;
      if (b >= 32 && b <= 126) out.writeCharCode(b);
    }
    return out.toString().trim();
  }

  void _ingest(String line, {required bool fromTcp}) {
    if (!mounted) return;
    if (!nmeaChecksumOk(line)) {
      _badChecksum++;
      setState(() {});
      return;
    }
    _received++;
    if (fromTcp) _tcpLines++;

    final now = DateTime.now();
    _windowStart ??= now;
    if (now.difference(_windowStart!).inMilliseconds >= 1000) {
      _rateHz = _windowCount.toDouble();
      _windowStart = now;
      _windowCount = 0;
    }
    _windowCount++;

    if (_paused) {
      setState(() {});
      return;
    }
    setState(() {
      _lines.insert(0, line);
      if (_lines.length > 300) {
        _lines.removeRange(300, _lines.length);
      }
    });
  }

  Future<void> _toggleTcp() async {
    if (_tcp != null) {
      await _tcpSub?.cancel();
      _tcp?.destroy();
      _tcp = null;
      _tcpSub = null;
      _tcpAcc = '';
      if (mounted) setState(() {});
      return;
    }
    final host = _ipController.text.trim();
    if (host.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Enter the terminal Wi-Fi IP from DIAGNOSTICS'),
        ),
      );
      return;
    }
    try {
      final socket = await Socket.connect(
        host,
        kGatewayTcpPort,
        timeout: const Duration(seconds: 5),
      );
      _tcp = socket;
      _tcpSub = socket.listen(
        (data) {
          _tcpAcc += String.fromCharCodes(data);
          // A talker that never sends a newline must not grow this forever.
          if (_tcpAcc.length > 4096) {
            _tcpAcc = _tcpAcc.substring(_tcpAcc.length - 1024);
          }
          while (true) {
            final cr = _tcpAcc.indexOf('\n');
            if (cr < 0) break;
            var line = _tcpAcc.substring(0, cr);
            _tcpAcc = _tcpAcc.substring(cr + 1);
            if (line.endsWith('\r')) {
              line = line.substring(0, line.length - 1);
            }
            line = line.trim();
            if (line.isNotEmpty) _ingest(line, fromTcp: true);
          }
        },
        onError: (e) {
          debugPrint('TCP NMEA error: $e');
          _tcp?.destroy();
          _tcp = null;
          if (mounted) setState(() {});
        },
        onDone: () {
          _tcp = null;
          if (mounted) setState(() {});
        },
      );
      if (mounted) setState(() {});
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text('TCP connect failed: $e')));
      }
    }
  }

  List<String> get _visible {
    final f = _filter.trim().toUpperCase();
    if (f.isEmpty) return _lines;
    return _lines.where((l) => l.toUpperCase().contains(f)).toList();
  }

  Future<void> _copyAll() async {
    final text = _visible.join('\n');
    await Clipboard.setData(ClipboardData(text: text));
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Copied ${_visible.length} lines')),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final visible = _visible;
    return Scaffold(
      appBar: AppBar(
        title: const Text('Sentences'),
        actions: [
          IconButton(
            tooltip: _paused ? 'Resume' : 'Pause',
            onPressed: () => setState(() => _paused = !_paused),
            icon: Icon(_paused ? Icons.play_arrow : Icons.pause),
          ),
          IconButton(
            tooltip: 'Clear',
            onPressed: () => setState(() {
              _lines.clear();
              _received = 0;
              _badChecksum = 0;
              _tcpLines = 0;
            }),
            icon: const Icon(Icons.delete_outline),
          ),
          IconButton(
            tooltip: 'Copy visible',
            onPressed: _copyAll,
            icon: const Icon(Icons.copy),
          ),
        ],
      ),
      body: Column(
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(12, 12, 12, 4),
            child: TextField(
              controller: _filterController,
              decoration: const InputDecoration(
                labelText: 'Filter (RMC, DPT, !AI, …)',
                border: OutlineInputBorder(),
                isDense: true,
              ),
              onChanged: (v) => setState(() => _filter = v),
            ),
          ),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
            child: Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: _ipController,
                    decoration: const InputDecoration(
                      labelText: 'Terminal IP (TCP 10110)',
                      border: OutlineInputBorder(),
                      isDense: true,
                    ),
                  ),
                ),
                const SizedBox(width: 8),
                FilledButton(
                  onPressed: _toggleTcp,
                  child: Text(_tcp == null ? 'TCP' : 'Hang up'),
                ),
              ],
            ),
          ),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
            child: Align(
              alignment: Alignment.centerLeft,
              child: Text(
                'BLE ${_subscribed ? "on" : "off"}  ·  '
                'rx $_received  ·  bad $_badChecksum  ·  '
                'tcp $_tcpLines  ·  ~${_rateHz.toStringAsFixed(0)} Hz'
                '${_paused ? "  ·  PAUSED" : ""}',
                style: Theme.of(context).textTheme.bodySmall,
              ),
            ),
          ),
          const Divider(height: 1),
          Expanded(
            child: visible.isEmpty
                ? const Center(
                    child: Text(
                      'Waiting for sentences…\n'
                      'Sim lines need MTU>23 for full RMC, or use TCP.',
                      textAlign: TextAlign.center,
                    ),
                  )
                : ListView.builder(
                    itemCount: visible.length,
                    itemBuilder: (context, i) {
                      return Padding(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 12,
                          vertical: 4,
                        ),
                        child: Text(
                          visible[i],
                          style: const TextStyle(
                            fontFamily: 'monospace',
                            fontSize: 12,
                          ),
                        ),
                      );
                    },
                  ),
          ),
        ],
      ),
    );
  }
}
