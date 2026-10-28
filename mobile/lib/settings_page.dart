import 'dart:async';

import 'package:flutter/material.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:url_launcher/url_launcher.dart';

import 'app_settings.dart';
import 'ui_kit.dart';

const kDocsUrl =
    'https://github.com/l-m-y-c/boat-terminal/blob/main/docs/06-phone-app-and-pairing.md';

const kStartTabLabels = {
  StartTab.boat: 'Boat',
  StartTab.instruments: 'Instruments',
  StartTab.nmeaData: 'NMEA data',
  StartTab.network: 'Network',
  StartTab.diagnostics: 'Diagnostics',
};

/// The member's preferences for this phone. Every change applies at once
/// and is kept for the next launch; nothing here changes the terminal.
class SettingsPage extends StatelessWidget {
  const SettingsPage({
    super.key,
    required this.settings,
    this.savedBoat,
    this.onForget,
    this.terminalId,
    this.onOpenDiagnostics,
    this.version,
  });

  final AppSettings settings;

  /// The boat this phone rejoins on launch, as a title.
  final String? savedBoat;
  final Future<void> Function()? onForget;
  final String? terminalId;
  final VoidCallback? onOpenDiagnostics;

  /// For tests; otherwise read from the installed package.
  final String? version;

  @override
  Widget build(BuildContext context) {
    final pal = Palette.of(context);
    return Scaffold(
      backgroundColor: pal.page,
      appBar: AppBar(title: const Text('Settings')),
      body: ListenableBuilder(
        listenable: settings,
        builder: (context, _) => ListView(
          key: const ValueKey('settings'),
          padding: const EdgeInsets.fromLTRB(20, 16, 20, 32),
          children: [
            const SectionLabel('Boat'),
            const SizedBox(height: 10),
            _Group(
              children: [
                ListTile(
                  key: const ValueKey('settings-saved-boat'),
                  leading: const Icon(Icons.sailing_outlined),
                  title: const Text('Saved boat'),
                  subtitle: Text(
                    savedBoat == null
                        ? 'None. The app looks for nearby terminals.'
                        : '$savedBoat. Rejoined when the app opens.',
                  ),
                  trailing: savedBoat == null || onForget == null
                      ? null
                      : TextButton(
                          key: const ValueKey('settings-forget'),
                          onPressed: () => _confirmForget(context),
                          child: const Text('Forget'),
                        ),
                ),
                if (onOpenDiagnostics != null)
                  ListTile(
                    key: const ValueKey('settings-diagnostics'),
                    leading: const Icon(Icons.troubleshoot),
                    title: const Text('Diagnostics'),
                    subtitle: const Text('Every link, and a report to share'),
                    trailing: const Icon(Icons.chevron_right),
                    onTap: onOpenDiagnostics,
                  ),
              ],
            ),
            const SizedBox(height: 22),
            const SectionLabel('Joining'),
            const SizedBox(height: 10),
            _Group(
              children: [
                SwitchListTile(
                  key: const ValueKey('settings-auto-join'),
                  secondary: const Icon(Icons.timer_outlined),
                  title: const Text('Join the nearest boat automatically'),
                  subtitle: Text(
                    settings.autoJoin
                        ? 'After a ${settings.countdownSeconds} s countdown '
                              'you can cancel'
                        : 'Always show the list of boats',
                  ),
                  value: settings.autoJoin,
                  onChanged: settings.setAutoJoin,
                ),
                if (settings.autoJoin)
                  _ChoiceRow<int>(
                    keyName: 'settings-countdown',
                    label: 'Countdown',
                    value: settings.countdownSeconds,
                    choices: {
                      for (final c in AppSettings.countdownChoices) c: '$c s',
                    },
                    onChanged: settings.setCountdownSeconds,
                  ),
                SwitchListTile(
                  key: const ValueKey('settings-splash'),
                  secondary: const Icon(Icons.celebration_outlined),
                  title: const Text('Arrival screen'),
                  subtitle: const Text('Show the boat\'s name when joined'),
                  value: settings.arrivalSplash,
                  onChanged: settings.setArrivalSplash,
                ),
                _ChoiceRow<StartTab>(
                  keyName: 'settings-start-tab',
                  label: 'Open on',
                  value: settings.startTab,
                  choices: kStartTabLabels,
                  onChanged: settings.setStartTab,
                ),
              ],
            ),
            const SizedBox(height: 22),
            const SectionLabel('Instruments'),
            const SizedBox(height: 10),
            _Group(
              children: [
                _ChoiceRow<SpeedUnit>(
                  keyName: 'settings-speed',
                  label: 'Speed and wind',
                  value: settings.speedUnit,
                  choices: const {
                    SpeedUnit.knots: 'Knots',
                    SpeedUnit.metresPerSecond: 'm/s',
                  },
                  onChanged: settings.setSpeedUnit,
                ),
                _ChoiceRow<DepthUnit>(
                  keyName: 'settings-depth',
                  label: 'Depth',
                  value: settings.depthUnit,
                  choices: const {
                    DepthUnit.metres: 'Metres',
                    DepthUnit.feet: 'Feet',
                  },
                  onChanged: settings.setDepthUnit,
                ),
                SwitchListTile(
                  key: const ValueKey('settings-keep-on'),
                  secondary: const Icon(Icons.light_mode_outlined),
                  title: const Text('Keep the screen on'),
                  subtitle: const Text('While Instruments is showing'),
                  value: settings.keepScreenOn,
                  onChanged: settings.setKeepScreenOn,
                ),
              ],
            ),
            const SizedBox(height: 22),
            const SectionLabel('Appearance'),
            const SizedBox(height: 10),
            _Group(
              children: [
                _ChoiceRow<ThemeMode>(
                  keyName: 'settings-theme',
                  label: 'Theme',
                  value: settings.themeMode,
                  choices: const {
                    ThemeMode.light: 'Light',
                    ThemeMode.dark: 'Dark',
                    ThemeMode.system: 'System',
                  },
                  onChanged: settings.setThemeMode,
                ),
              ],
            ),
            const SizedBox(height: 22),
            const SectionLabel('About'),
            const SizedBox(height: 10),
            _Group(
              children: [
                ListTile(
                  key: const ValueKey('settings-version'),
                  leading: const Icon(Icons.info_outline),
                  title: const Text('App version'),
                  subtitle: version != null
                      ? Text(version!)
                      : FutureBuilder<PackageInfo>(
                          future: PackageInfo.fromPlatform(),
                          builder: (_, snap) => Text(
                            snap.hasData
                                ? '${snap.data!.version} (${snap.data!.buildNumber})'
                                : '…',
                          ),
                        ),
                ),
                ListTile(
                  key: const ValueKey('settings-terminal-id'),
                  leading: const Icon(Icons.memory),
                  title: const Text('Terminal ID'),
                  subtitle: Text(terminalId ?? 'Not connected'),
                ),
                ListTile(
                  key: const ValueKey('settings-docs'),
                  leading: const Icon(Icons.menu_book_outlined),
                  title: const Text('How the app works'),
                  subtitle: const Text('Pairing, links and troubleshooting'),
                  trailing: const Icon(Icons.open_in_new, size: 18),
                  onTap: () => unawaited(
                    launchUrl(
                      Uri.parse(kDocsUrl),
                      mode: LaunchMode.externalApplication,
                    ),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Future<void> _confirmForget(BuildContext context) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: Text('Forget $savedBoat?'),
        content: const Text(
          'The app will stop rejoining this boat when it opens. The current '
          'connection stays up.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(dialogContext, false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            key: const ValueKey('settings-forget-confirm'),
            onPressed: () => Navigator.pop(dialogContext, true),
            child: const Text('Forget'),
          ),
        ],
      ),
    );
    if (ok == true) await onForget?.call();
  }
}

/// A rounded card of rows, matching the Diagnostics link cards.
class _Group extends StatelessWidget {
  const _Group({required this.children});

  final List<Widget> children;

  @override
  Widget build(BuildContext context) {
    final pal = Palette.of(context);
    return Material(
      color: pal.card,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
        side: BorderSide(color: pal.line, width: 1.5),
      ),
      clipBehavior: Clip.antiAlias,
      child: Column(
        children: [
          for (var i = 0; i < children.length; i++) ...[
            if (i > 0) Divider(height: 1, color: pal.line),
            children[i],
          ],
        ],
      ),
    );
  }
}

/// A label with a segmented choice under it.
class _ChoiceRow<T> extends StatelessWidget {
  const _ChoiceRow({
    required this.keyName,
    required this.label,
    required this.value,
    required this.choices,
    required this.onChanged,
  });

  final String keyName;
  final String label;
  final T value;
  final Map<T, String> choices;
  final Future<void> Function(T) onChanged;

  @override
  Widget build(BuildContext context) {
    final pal = Palette.of(context);
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 12, 16, 14),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: TextStyle(color: pal.body, fontSize: 15)),
          const SizedBox(height: 10),
          if (choices.length > 3)
            SizedBox(
              width: double.infinity,
              child: Wrap(
                key: ValueKey(keyName),
                spacing: 8,
                runSpacing: 8,
                children: [
                  for (final e in choices.entries)
                    ChoiceChip(
                      label: Text(
                        e.value,
                        key: ValueKey('$keyName-${e.value}'),
                      ),
                      selected: e.key == value,
                      showCheckmark: false,
                      onSelected: (_) => unawaited(onChanged(e.key)),
                    ),
                ],
              ),
            )
          else
            SizedBox(
              width: double.infinity,
              child: SegmentedButton<T>(
                key: ValueKey(keyName),
                showSelectedIcon: false,
                segments: [
                  for (final e in choices.entries)
                    ButtonSegment<T>(
                      value: e.key,
                      label: Text(
                        e.value,
                        key: ValueKey('$keyName-${e.value}'),
                      ),
                    ),
                ],
                selected: {value},
                onSelectionChanged: (s) => unawaited(onChanged(s.first)),
              ),
            ),
        ],
      ),
    );
  }
}
