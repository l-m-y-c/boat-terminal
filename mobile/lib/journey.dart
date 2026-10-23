import 'package:flutter/material.dart';

import 'ui_kit.dart';

/// The stops between opening the app and chart apps showing the boat.
enum JourneyStop { find, connect, wifi, charts, arrived }

const kNavy = Color(0xFF001F3F);
const kGreen = Color(0xFF1E7F5C);
const kAmber = Color(0xFFB7791F);
const kRed = Color(0xFFB42318);
const kLine = Color(0xFFE2E8F0);

const _labels = {
  JourneyStop.find: 'Find',
  JourneyStop.connect: 'Connect',
  JourneyStop.wifi: 'Wi-Fi',
  JourneyStop.charts: 'NMEA',
};

/// Four stops on a line: done ones ticked, the current one ringed.
class JourneyBar extends StatelessWidget {
  const JourneyBar({
    super.key,
    required this.current,
    this.problem = false,
    this.compact = false,
  });

  final JourneyStop current;

  /// A slim strip once on board: the same stops, smaller.
  final bool compact;

  /// The current stop needs the user before it can go on.
  final bool problem;

  @override
  Widget build(BuildContext context) {
    final stops = _labels.keys.toList();
    return Semantics(
      label: current == JourneyStop.arrived
          ? 'All steps done'
          : 'Step ${current.index + 1} of ${stops.length}: ${_labels[current]}',
      child: ExcludeSemantics(
        child: Row(
          children: [
            for (final (i, stop) in stops.indexed) ...[
              if (i > 0)
                Expanded(
                  child: Padding(
                    padding: EdgeInsets.only(bottom: compact ? 17 : 22),
                    child: AnimatedContainer(
                      duration: const Duration(milliseconds: 300),
                      height: compact ? 2 : 3,
                      color: stop.index <= current.index
                          ? kGreen
                          : Palette.of(context).line,
                    ),
                  ),
                ),
              _Dot(
                label: _labels[stop]!,
                done: stop.index < current.index,
                now: stop == current,
                problem: problem && stop == current,
                compact: compact,
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class _Dot extends StatelessWidget {
  const _Dot({
    required this.label,
    required this.done,
    required this.now,
    required this.problem,
    this.compact = false,
  });

  final String label;
  final bool done;
  final bool now;
  final bool problem;
  final bool compact;

  @override
  Widget build(BuildContext context) {
    final color = problem
        ? kRed
        : done
        ? kGreen
        : now
        ? Palette.of(context).ink
        : Palette.of(context).faint;
    return SizedBox(
      width: 64,
      child: Column(
        children: [
          AnimatedContainer(
            duration: const Duration(milliseconds: 300),
            width: compact ? 20 : 30,
            height: compact ? 20 : 30,
            decoration: BoxDecoration(
              color: done || problem ? color : Palette.of(context).card,
              shape: BoxShape.circle,
              border: Border.all(color: color, width: now && !compact ? 3 : 2),
              boxShadow: now && !problem && !compact
                  ? [
                      BoxShadow(
                        color: kNavy.withValues(alpha: 0.18),
                        blurRadius: 0,
                        spreadRadius: 5,
                      ),
                    ]
                  : null,
            ),
            child: Icon(
              problem
                  ? Icons.priority_high
                  : done
                  ? Icons.check
                  : Icons.circle,
              size: done || problem ? (compact ? 13 : 18) : (compact ? 6 : 8),
              color: done || problem ? Colors.white : color,
            ),
          ),
          SizedBox(height: compact ? 3 : 6),
          Text(
            label,
            maxLines: 1,
            overflow: TextOverflow.fade,
            softWrap: false,
            style: TextStyle(
              fontSize: compact ? 11 : 12.5,
              fontWeight: now ? FontWeight.w700 : FontWeight.w500,
              color: now || done
                  ? Palette.of(context).ink
                  : Palette.of(context).faint,
            ),
          ),
        ],
      ),
    );
  }
}

/// One stop's content: what is happening, and the one thing to do about it.
class JourneyPanel extends StatelessWidget {
  const JourneyPanel({
    super.key,
    required this.icon,
    required this.title,
    required this.body,
    this.tone = kNavy,
    this.working = false,
    this.progress,
    this.primary,
    this.secondary = const [],
    this.footer,
  });

  final IconData icon;
  final String title;
  final String body;
  final Color tone;

  /// Waiting on the phone or the terminal, not on the user.
  final bool working;

  /// A known fraction (a countdown). Null shows an endless bar when [working].
  final double? progress;
  final Widget? primary;
  final List<Widget> secondary;
  final Widget? footer;

  @override
  Widget build(BuildContext context) {
    final tone = this.tone == kNavy ? Palette.of(context).ink : this.tone;
    return LayoutBuilder(
      builder: (context, box) => SingleChildScrollView(
        padding: const EdgeInsets.fromLTRB(28, 8, 28, 24),
        child: ConstrainedBox(
          constraints: BoxConstraints(minHeight: box.maxHeight - 32),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Center(
                child: Container(
                  width: 88,
                  height: 88,
                  decoration: BoxDecoration(
                    color: tone.withValues(alpha: 0.08),
                    shape: BoxShape.circle,
                  ),
                  child: Icon(icon, size: 44, color: tone),
                ),
              ),
              const SizedBox(height: 22),
              Text(
                title,
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                  fontWeight: FontWeight.w700,
                  color: Palette.of(context).ink,
                ),
              ),
              const SizedBox(height: 10),
              Text(
                body,
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodyLarge
                    ?.copyWith(color: Palette.of(context).muted, height: 1.45),
              ),
              if (working || progress != null) ...[
                const SizedBox(height: 26),
                ClipRRect(
                  borderRadius: BorderRadius.circular(6),
                  child: LinearProgressIndicator(
                    value: progress,
                    minHeight: 6,
                    backgroundColor: Palette.of(context).line,
                    color: tone,
                  ),
                ),
              ],
              if (primary != null) ...[const SizedBox(height: 28), primary!],
              if (secondary.isNotEmpty) ...[
                const SizedBox(height: 8),
                ...secondary,
              ],
              if (footer != null) ...[const SizedBox(height: 20), footer!],
            ],
          ),
        ),
      ),
    );
  }
}

/// The main button on a panel.
class PrimaryButton extends StatelessWidget {
  const PrimaryButton({
    super.key,
    required this.label,
    required this.onPressed,
    this.icon,
  });

  final String label;
  final VoidCallback? onPressed;
  final IconData? icon;

  @override
  Widget build(BuildContext context) {
    final style = FilledButton.styleFrom(
      backgroundColor: Theme.of(context).brightness == Brightness.dark
          ? const Color(0xFF2F5D8C)
          : kNavy,
      foregroundColor: Colors.white,
      minimumSize: const Size.fromHeight(52),
      textStyle: const TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14)),
    );
    final i = icon;
    return i == null
        ? FilledButton(onPressed: onPressed, style: style, child: Text(label))
        : FilledButton.icon(
            onPressed: onPressed,
            style: style,
            icon: Icon(i),
            label: Text(label),
          );
  }
}

/// A quieter second choice under the main button.
class SecondaryButton extends StatelessWidget {
  const SecondaryButton({
    super.key,
    required this.label,
    required this.onPressed,
    this.icon,
  });

  final String label;
  final VoidCallback? onPressed;
  final IconData? icon;

  @override
  Widget build(BuildContext context) {
    final style = OutlinedButton.styleFrom(
      foregroundColor: Palette.of(context).ink,
      minimumSize: const Size.fromHeight(50),
      side: BorderSide(color: Palette.of(context).line, width: 1.5),
      textStyle: const TextStyle(fontSize: 15, fontWeight: FontWeight.w600),
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14)),
    );
    final i = icon;
    return i == null
        ? OutlinedButton(onPressed: onPressed, style: style, child: Text(label))
        : OutlinedButton.icon(
            onPressed: onPressed,
            style: style,
            icon: Icon(i),
            label: Text(label),
          );
  }
}

/// One choice on the arrived screen.
class OptionTile extends StatelessWidget {
  const OptionTile({
    super.key,
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.onTap,
    this.trailing,
  });

  final IconData icon;
  final String title;
  final String subtitle;
  final VoidCallback onTap;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 12),
      child: Material(
        color: Palette.of(context).card,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(16),
          side: BorderSide(color: Palette.of(context).line, width: 1.5),
        ),
        child: InkWell(
          borderRadius: BorderRadius.circular(16),
          onTap: onTap,
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 16),
            child: Row(
              children: [
                Container(
                  width: 44,
                  height: 44,
                  decoration: BoxDecoration(
                    color: kNavy.withValues(alpha: 0.07),
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: Icon(icon, color: Palette.of(context).ink),
                ),
                const SizedBox(width: 14),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        title,
                        style: TextStyle(
                          fontSize: 16.5,
                          fontWeight: FontWeight.w600,
                          color: Palette.of(context).ink,
                        ),
                      ),
                      const SizedBox(height: 2),
                      Text(
                        subtitle,
                        style: TextStyle(
                          color: Palette.of(context).muted,
                          height: 1.3,
                        ),
                      ),
                    ],
                  ),
                ),
                trailing ??
                    Icon(Icons.chevron_right, color: Palette.of(context).faint),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
