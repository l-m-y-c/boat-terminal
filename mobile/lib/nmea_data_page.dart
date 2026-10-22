import 'dart:async';

import 'package:flutter/material.dart';

import 'counter_help.dart';
import 'terminal_stats.dart';
import 'ui_kit.dart';

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

/// Explains the data source pill: the simulation sheet while simulating,
/// otherwise the state in a sentence.
void showDataSourceSheet(BuildContext context, TerminalStats s) {
  if (s.simulated) {
    unawaited(showSimulationSheet(context));
    return;
  }
  unawaited(
    showInfoSheet(
      context,
      title: _headline(s),
      icon: Icons.lan_outlined,
      tone: toneColor(s.tone),
      paragraphs: [s.summary],
    ),
  );
}

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
/// Every panel opens an explanation. No app bar: the home shell owns it.
class NmeaDataView extends StatelessWidget {
  const NmeaDataView({
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
    return s == null ? _waiting(context) : _body(context, s);
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
                  : 'NMEA data needs the Bluetooth link to the terminal. '
                        'Reconnect on the Boat tab, or update the '
                        'terminal\'s firmware if it is old.',
              textAlign: TextAlign.center,
              style: const TextStyle(fontSize: 16, height: 1.4),
            ),
          ],
        ),
      ),
    );
  }

  Widget _body(BuildContext context, TerminalStats s) {
    final updated = updatedAt == null
        ? ''
        : ' · updated ${DateTime.now().difference(updatedAt!).inSeconds} s ago';
    return ListView(
      padding: const EdgeInsets.fromLTRB(20, 12, 20, 32),
      children: [
        Row(
          children: [
            InkWell(
              key: const ValueKey('data-source-pill'),
              borderRadius: BorderRadius.circular(20),
              onTap: () => showDataSourceSheet(context, s),
              child: DataStatusPill(stats: s),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Text(
                'Tap a panel for help',
                textAlign: TextAlign.right,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: Palette.of(context).faint,
                  fontSize: 12.5,
                ),
              ),
            ),
          ],
        ),
        const SizedBox(height: 12),
        _IntegrityLine(stats: s),
        _Section(
          title: 'Traffic',
          metrics: [
            _Metric('mps', 'Messages / s', formatCount(s.messagesPerSecond)),
            _Metric('fps', 'Frames / s', formatCount(s.framesPerSecond)),
            _Metric(
              'fps_peak',
              'Peak frames / s',
              formatCount(s.framesPerSecondPeak),
            ),
            _Metric('frames', 'Frames received', formatCount(s.frames)),
            _Metric('pgns', 'PGN types', formatCount(s.pgnTypes)),
            _Metric('devices', 'Devices', formatCount(s.devices)),
          ],
        ),
        _Section(
          title: 'Integrity',
          metrics: [
            _Metric(
              'lost',
              'Lost frames',
              formatCount(s.lost),
              tone: s.lost == 0 ? Tone.good : Tone.bad,
            ),
            _Metric(
              'bus_err',
              'Bus errors',
              formatCount(s.busErrors),
              tone: s.busErrors == 0 ? null : Tone.caution,
            ),
            _Metric('rec', 'Error counter', formatCount(s.receiveErrorCounter)),
          ],
          footer: _BufferBar(
            helpKey: 'rx_buffer',
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
            _Metric('apps', 'Connected', formatCount(s.chartApps)),
            _Metric('out', 'Sentences sent', formatCount(s.sentencesOut)),
            _Metric(
              'not_delivered',
              'Not delivered',
              formatCount(s.notDelivered),
              tone: s.notDelivered == 0 ? null : Tone.caution,
            ),
          ],
          footer: _BufferBar(
            helpKey: 'app_buffer',
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
          style: TextStyle(color: Palette.of(context).faint, fontSize: 13),
        ),
      ],
    );
  }
}

void _openHelp(BuildContext context, String key) {
  final help = counterHelp[key];
  if (help == null) return;
  unawaited(
    showInfoSheet(
      context,
      title: help.title,
      icon: Icons.help_outline,
      paragraphs: help.paragraphs,
    ),
  );
}

String _kb(int bytes) =>
    bytes < 1024 ? '$bytes B' : '${(bytes / 1024).toStringAsFixed(1)} KB';

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
          SectionLabel(title),
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
  const _Metric(this.helpKey, this.label, this.value, {this.tone});

  final String helpKey;
  final String label;
  final String value;
  final Tone? tone;

  @override
  Widget build(BuildContext context) {
    final color = tone == null ? Palette.of(context).ink : toneColor(tone!);
    return _Tappable(
      helpKey: helpKey,
      label: label,
      padding: const EdgeInsets.fromLTRB(14, 12, 14, 12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Expanded(
                child: Text(
                  label,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: Palette.of(context).muted,
                    fontSize: 12.5,
                  ),
                ),
              ),
              Icon(
                Icons.info_outline,
                size: 14,
                color: Palette.of(context).faint,
              ),
            ],
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
    required this.helpKey,
    required this.label,
    required this.share,
    required this.detail,
  });

  final String helpKey;
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
    return _Tappable(
      helpKey: helpKey,
      label: label,
      padding: const EdgeInsets.all(14),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Text(
                label,
                style: TextStyle(
                  fontWeight: FontWeight.w600,
                  color: Palette.of(context).ink,
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
              backgroundColor: Palette.of(context).line,
              color: toneColor(tone),
            ),
          ),
          const SizedBox(height: 8),
          Text(
            detail,
            style: TextStyle(color: Palette.of(context).muted, fontSize: 13),
          ),
        ],
      ),
    );
  }
}

/// A white panel that opens its help sheet when tapped.
class _Tappable extends StatelessWidget {
  const _Tappable({
    required this.helpKey,
    required this.label,
    required this.padding,
    required this.child,
  });

  final String helpKey;
  final String label;
  final EdgeInsets padding;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    final shape = RoundedRectangleBorder(
      borderRadius: BorderRadius.circular(14),
      side: BorderSide(color: Palette.of(context).line),
    );
    return Semantics(
      button: true,
      hint: 'What is $label?',
      child: Material(
        color: Palette.of(context).card,
        shape: shape,
        child: InkWell(
          key: ValueKey('metric-$helpKey'),
          customBorder: shape,
          onTap: () => _openHelp(context, helpKey),
          child: Padding(padding: padding, child: child),
        ),
      ),
    );
  }
}
