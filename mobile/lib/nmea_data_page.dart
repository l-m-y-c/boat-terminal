import 'package:flutter/material.dart';

import 'terminal_stats.dart';

const _navy = Color(0xFF001F3F);
const _border = Color(0xFFE2E8F0);

Color toneColor(Tone tone) => switch (tone) {
  Tone.good => const Color(0xFF1E7F5C),
  Tone.caution => const Color(0xFFB7791F),
  Tone.bad => const Color(0xFFB42318),
  Tone.neutral => const Color(0xFF5C6B7A),
};

/// "Live · NMEA 2000" / "Simulation" / "Waiting for boat data", with a dot.
class DataStatusPill extends StatelessWidget {
  const DataStatusPill({super.key, required this.stats});

  final TerminalStats stats;

  @override
  Widget build(BuildContext context) {
    final color = toneColor(stats.tone);
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(20),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            width: 8,
            height: 8,
            decoration: BoxDecoration(color: color, shape: BoxShape.circle),
          ),
          const SizedBox(width: 8),
          Text(
            stats.pill,
            style: TextStyle(
              color: color,
              fontWeight: FontWeight.w600,
              fontSize: 13,
            ),
          ),
        ],
      ),
    );
  }
}

String _headline(TerminalStats s) => switch (s.state) {
  BusState.live => 'Live boat data',
  BusState.simulated || BusState.paused => 'Simulated data',
  BusState.opening => 'Starting up',
  BusState.waiting || BusState.quiet => 'Waiting for boat data',
  BusState.busOff || BusState.failed => 'NMEA 2000 bus fault',
};

class _IntegrityLine extends StatelessWidget {
  const _IntegrityLine({required this.stats});

  final TerminalStats stats;

  @override
  Widget build(BuildContext context) {
    final clean = stats.lost == 0;
    final color = toneColor(clean ? Tone.good : Tone.bad);
    final text = clean
        ? 'No frames lost since the terminal started'
        : '${formatCount(stats.lost)} ${stats.lost == 1 ? 'frame' : 'frames'} lost '
              'since the terminal started';
    return Row(
      children: [
        Icon(
          clean ? Icons.verified_outlined : Icons.error_outline,
          size: 18,
          color: color,
        ),
        const SizedBox(width: 8),
        Expanded(
          child: Text(
            text,
            style: TextStyle(color: color, fontWeight: FontWeight.w600),
          ),
        ),
      ],
    );
  }
}

/// The terminal's data source, traffic, integrity and delivery counters.
class BoatDataPage extends StatelessWidget {
  const BoatDataPage({
    super.key,
    required this.stats,
    required this.updatedAt,
    required this.supported,
  });

  final TerminalStats? stats;
  final DateTime? updatedAt;

  /// False when the terminal's firmware has no statistics characteristic.
  final bool supported;

  @override
  Widget build(BuildContext context) {
    final s = stats;
    return Scaffold(
      appBar: AppBar(title: const Text('Boat data')),
      body: SafeArea(child: s == null ? _waiting(context) : _body(context, s)),
    );
  }

  Widget _waiting(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(28),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (supported) const CircularProgressIndicator(),
            const SizedBox(height: 20),
            Text(
              supported
                  ? 'Reading the terminal…'
                  : 'This terminal does not report its data source yet. '
                        'Update its firmware.',
              textAlign: TextAlign.center,
              style: const TextStyle(fontSize: 16, height: 1.4),
            ),
          ],
        ),
      ),
    );
  }

  Widget _body(BuildContext context, TerminalStats s) {
    final color = toneColor(s.tone);
    final updated = updatedAt == null
        ? ''
        : ' · updated ${DateTime.now().difference(updatedAt!).inSeconds} s ago';
    return ListView(
      padding: const EdgeInsets.fromLTRB(20, 20, 20, 32),
      children: [
        Container(
          padding: const EdgeInsets.all(20),
          decoration: BoxDecoration(
            color: color.withValues(alpha: 0.07),
            borderRadius: BorderRadius.circular(20),
            border: Border.all(color: color.withValues(alpha: 0.35)),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              DataStatusPill(stats: s),
              const SizedBox(height: 14),
              Text(
                _headline(s),
                style: Theme.of(context).textTheme.headlineSmall
                    ?.copyWith(fontWeight: FontWeight.w700, color: _navy),
              ),
              const SizedBox(height: 6),
              Text(
                s.summary,
                style: const TextStyle(color: Colors.black87, height: 1.4),
              ),
              const SizedBox(height: 14),
              _IntegrityLine(stats: s),
            ],
          ),
        ),
        if (s.simulated) ...[
          const SizedBox(height: 14),
          const _Note(
            icon: Icons.info_outline,
            text:
                'To use the boat\'s own instruments, tap the SIMULATION badge on '
                'the terminal\'s top bar and choose NMEA 2000.',
          ),
        ],
        _Section(
          title: 'Traffic',
          metrics: [
            _Metric('Messages / s', formatCount(s.messagesPerSecond)),
            _Metric('Frames / s', formatCount(s.framesPerSecond)),
            _Metric('Peak frames / s', formatCount(s.framesPerSecondPeak)),
            _Metric('Frames received', formatCount(s.frames)),
            _Metric('PGN types', formatCount(s.pgnTypes)),
            _Metric('Devices', formatCount(s.devices)),
          ],
        ),
        _Section(
          title: 'Integrity',
          metrics: [
            _Metric(
              'Lost frames',
              formatCount(s.lost),
              tone: s.lost == 0 ? Tone.good : Tone.bad,
            ),
            _Metric(
              'Bus errors',
              formatCount(s.busErrors),
              tone: s.busErrors == 0 ? null : Tone.caution,
            ),
            _Metric('Error counter', formatCount(s.receiveErrorCounter)),
          ],
          footer: _BufferBar(
            label: 'Receive buffer',
            share: s.bufferPeakShare,
            detail:
                'Peak ${formatCount(s.bufferPeak)} of ${formatCount(s.bufferCapacity)} '
                'frames · now ${formatCount(s.bufferNow)}',
          ),
        ),
        _Section(
          title: 'Chart apps',
          metrics: [
            _Metric('Connected', formatCount(s.chartApps)),
            _Metric('Sentences sent', formatCount(s.sentencesOut)),
            _Metric(
              'Not delivered',
              formatCount(s.notDelivered),
              tone: s.notDelivered == 0 ? null : Tone.caution,
            ),
          ],
          footer: _BufferBar(
            label: 'App buffer',
            share: s.appBufferPeakShare,
            detail:
                'Peak ${_kb(s.appBufferPeak)} of ${_kb(s.appBufferCapacity)} per app',
          ),
        ),
        const SizedBox(height: 20),
        Text(
          'Terminal up ${formatUptime(s.uptimeSeconds)} · '
          'last frame ${formatAge(s.lastFrameAgeMs)}$updated',
          style: const TextStyle(color: Colors.black45, fontSize: 13),
        ),
      ],
    );
  }
}

String _kb(int bytes) =>
    bytes < 1024 ? '$bytes B' : '${(bytes / 1024).toStringAsFixed(1)} KB';

class _Note extends StatelessWidget {
  const _Note({required this.icon, required this.text});

  final IconData icon;
  final String text;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: const Color(0xFFF1F5F9),
        borderRadius: BorderRadius.circular(14),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, size: 20, color: Colors.black54),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              text,
              style: const TextStyle(color: Colors.black87, height: 1.4),
            ),
          ),
        ],
      ),
    );
  }
}

class _Section extends StatelessWidget {
  const _Section({required this.title, required this.metrics, this.footer});

  final String title;
  final List<_Metric> metrics;
  final Widget? footer;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(top: 26),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            title.toUpperCase(),
            style: const TextStyle(
              fontSize: 12,
              letterSpacing: 1.1,
              fontWeight: FontWeight.w700,
              color: Colors.black45,
            ),
          ),
          const SizedBox(height: 10),
          LayoutBuilder(
            builder: (context, box) {
              final columns = box.maxWidth >= 520 ? 3 : 2;
              const gap = 10.0;
              final width = (box.maxWidth - gap * (columns - 1)) / columns;
              return Wrap(
                spacing: gap,
                runSpacing: gap,
                children: [
                  for (final m in metrics) SizedBox(width: width, child: m),
                ],
              );
            },
          ),
          if (footer != null) ...[const SizedBox(height: 10), footer!],
        ],
      ),
    );
  }
}

class _Metric extends StatelessWidget {
  const _Metric(this.label, this.value, {this.tone});

  final String label;
  final String value;
  final Tone? tone;

  @override
  Widget build(BuildContext context) {
    final color = tone == null ? _navy : toneColor(tone!);
    return Container(
      padding: const EdgeInsets.fromLTRB(14, 12, 14, 12),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: _border),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            label,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(color: Colors.black54, fontSize: 12.5),
          ),
          const SizedBox(height: 4),
          FittedBox(
            fit: BoxFit.scaleDown,
            alignment: Alignment.centerLeft,
            child: Text(
              value,
              style: TextStyle(
                fontSize: 22,
                fontWeight: FontWeight.w700,
                color: color,
                fontFeatures: const [FontFeature.tabularFigures()],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _BufferBar extends StatelessWidget {
  const _BufferBar({
    required this.label,
    required this.share,
    required this.detail,
  });

  final String label;
  final double share;
  final String detail;

  @override
  Widget build(BuildContext context) {
    final tone = share >= 0.75
        ? Tone.bad
        : share >= 0.5
        ? Tone.caution
        : Tone.good;
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: _border),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Text(
                label,
                style: const TextStyle(
                  fontWeight: FontWeight.w600,
                  color: _navy,
                ),
              ),
              const Spacer(),
              Text(
                '${(share * 100).toStringAsFixed(share > 0 && share < 0.01 ? 1 : 0)}%',
                style: TextStyle(
                  fontWeight: FontWeight.w600,
                  color: toneColor(tone),
                ),
              ),
            ],
          ),
          const SizedBox(height: 10),
          ClipRRect(
            borderRadius: BorderRadius.circular(6),
            child: LinearProgressIndicator(
              value: share,
              minHeight: 8,
              backgroundColor: const Color(0xFFE2E8F0),
              color: toneColor(tone),
            ),
          ),
          const SizedBox(height: 8),
          Text(
            detail,
            style: const TextStyle(color: Colors.black54, fontSize: 13),
          ),
        ],
      ),
    );
  }
}
