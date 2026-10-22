import 'package:flutter/material.dart';

import 'journey.dart';

/// A bottom sheet with a title and a few paragraphs: the one way the app
/// explains something in depth, so every "what is this?" looks the same.
Future<void> showInfoSheet(
  BuildContext context, {
  required String title,
  required List<String> paragraphs,
  IconData icon = Icons.info_outline,
  Color tone = kNavy,
  Widget? footer,
}) {
  return showModalBottomSheet<void>(
    context: context,
    isScrollControlled: true,
    showDragHandle: true,
    backgroundColor: Colors.white,
    builder: (sheetContext) => SafeArea(
      child: ConstrainedBox(
        constraints: BoxConstraints(
          maxHeight: MediaQuery.of(sheetContext).size.height * 0.8,
        ),
        child: SingleChildScrollView(
          padding: const EdgeInsets.fromLTRB(24, 0, 24, 24),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisSize: MainAxisSize.min,
            children: [
              Row(
                children: [
                  Icon(icon, color: tone),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Text(
                      title,
                      style: Theme.of(sheetContext).textTheme.titleLarge
                          ?.copyWith(fontWeight: FontWeight.w700, color: kNavy),
                    ),
                  ),
                ],
              ),
              for (final p in paragraphs) ...[
                const SizedBox(height: 12),
                Text(
                  p,
                  style: const TextStyle(
                    fontSize: 15,
                    height: 1.45,
                    color: Colors.black87,
                  ),
                ),
              ],
              if (footer != null) ...[const SizedBox(height: 16), footer],
            ],
          ),
        ),
      ),
    ),
  );
}

/// What the Simulation badge explains.
const kSimulationParagraphs = [
  'The terminal is simulating a boat. Position, speed, course, depth and '
      'wind are made up by the terminal, not read from the boat\'s '
      'instruments.',
  'Chart apps on this phone get the same simulated data, so do not '
      'navigate by it.',
  'To use the boat\'s own instruments, tap the SIMULATION badge on the '
      'terminal\'s top bar and choose NMEA 2000. The terminal restarts to '
      'switch.',
];

Future<void> showSimulationSheet(BuildContext context) => showInfoSheet(
  context,
  title: 'Simulation',
  icon: Icons.science_outlined,
  tone: kAmber,
  paragraphs: kSimulationParagraphs,
);

/// The small amber "Simulation" tag. Tapping it explains what that means.
class SimulationBadge extends StatelessWidget {
  const SimulationBadge({super.key, this.onDark = false});

  /// On the navy app bar rather than a light page.
  final bool onDark;

  @override
  Widget build(BuildContext context) {
    final fg = onDark ? const Color(0xFFFFD48A) : kAmber;
    return Semantics(
      button: true,
      label: 'Simulation. What does this mean?',
      child: ExcludeSemantics(
        child: Material(
          color: fg.withValues(alpha: onDark ? 0.18 : 0.12),
          shape: StadiumBorder(
            side: BorderSide(color: fg.withValues(alpha: 0.6)),
          ),
          child: InkWell(
            key: const ValueKey('simulation-badge'),
            customBorder: const StadiumBorder(),
            onTap: () => showSimulationSheet(context),
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 3),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(Icons.science_outlined, size: 14, color: fg),
                  const SizedBox(width: 4),
                  Text(
                    'Simulation',
                    style: TextStyle(
                      color: fg,
                      fontSize: 12,
                      fontWeight: FontWeight.w700,
                      letterSpacing: 0.2,
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// The app bar title: the boat's name, with the Simulation badge beside it
/// while the terminal simulates. The same on every screen.
class BoatTitle extends StatelessWidget {
  const BoatTitle({super.key, required this.name, this.simulated = false});

  final String name;
  final bool simulated;

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Flexible(child: Text(name, overflow: TextOverflow.ellipsis)),
        if (simulated) ...[
          const SizedBox(width: 8),
          const SimulationBadge(onDark: true),
        ],
      ],
    );
  }
}

/// Small upper-case heading above a group of panels.
class SectionLabel extends StatelessWidget {
  const SectionLabel(this.text, {super.key});

  final String text;

  @override
  Widget build(BuildContext context) => Text(
    text.toUpperCase(),
    style: const TextStyle(
      fontSize: 12,
      letterSpacing: 1.1,
      fontWeight: FontWeight.w700,
      color: Colors.black45,
    ),
  );
}

/// Surface and text colours that follow the light or dark theme. The brand
/// colours (kNavy, kGreen, kAmber, kRed) stay the same in both; [ink] is
/// navy on light pages and near-white on dark ones.
class Palette {
  const Palette({
    required this.page,
    required this.card,
    required this.line,
    required this.ink,
    required this.body,
    required this.muted,
    required this.faint,
  });

  final Color page;
  final Color card;
  final Color line;
  final Color ink;
  final Color body;
  final Color muted;
  final Color faint;

  static const light = Palette(
    page: Color(0xFFF7F9FB),
    card: Colors.white,
    line: kLine,
    ink: kNavy,
    body: Colors.black87,
    muted: Colors.black54,
    faint: Colors.black45,
  );

  static const dark = Palette(
    page: Color(0xFF0B1220),
    card: Color(0xFF141D2C),
    line: Color(0xFF26324A),
    ink: Color(0xFFE6EDF5),
    body: Color(0xFFD3DCE6),
    muted: Color(0xFF9AA8B8),
    faint: Color(0xFF7D8B9C),
  );

  static Palette of(BuildContext context) =>
      Theme.of(context).brightness == Brightness.dark ? dark : light;
}
