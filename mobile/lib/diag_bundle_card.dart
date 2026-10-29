// The Collect diagnostics card on the Diagnostics tab.
import 'dart:async';

import 'package:flutter/material.dart';
import 'package:share_plus/share_plus.dart';

import 'diag_bundle.dart';
import 'ui_kit.dart';

class DiagBundleCard extends StatefulWidget {
  const DiagBundleCard({super.key, required this.controller});

  final DiagBundleController controller;

  @override
  State<DiagBundleCard> createState() => _DiagBundleCardState();
}

class _DiagBundleCardState extends State<DiagBundleCard> {
  bool _showAll = false;

  @override
  void initState() {
    super.initState();
    unawaited(widget.controller.refresh());
  }

  Future<void> _collect(String reason, {String? note}) async {
    final r = await widget.controller.collect(reason, note: note);
    if (!mounted || r == null) return;
    // No action button: with one, Android accessibility keeps the snackbar up
    // indefinitely and it covers Mark problem. Share is on the list below.
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(
        SnackBar(
          behavior: SnackBarBehavior.floating,
          duration: const Duration(seconds: 3),
          content: Text('Saved ${_name(r.file.path)}: ${r.summary}'),
        ),
      );
  }

  Future<void> _mark() async {
    final text = TextEditingController();
    final note = await showDialog<String>(
      context: context,
      builder: (c) => AlertDialog(
        title: const Text('Mark a problem'),
        content: TextField(
          key: const ValueKey('diag-mark-note'),
          controller: text,
          autofocus: true,
          decoration: const InputDecoration(
            hintText: 'What went wrong? (optional)',
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(c),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(c, text.text.trim()),
            child: const Text('Save snapshot'),
          ),
        ],
      ),
    );
    text.dispose();
    if (note != null) await _collect('problem', note: note);
  }

  Future<void> _share(String path) async {
    try {
      await SharePlus.instance.share(
        ShareParams(
          files: [XFile(path, mimeType: 'application/zip')],
          subject: 'LMYC terminal diagnostics ${_name(path)}',
        ),
      );
    } catch (_) {}
  }

  static String _name(String path) => path.split('/').last;

  @override
  Widget build(BuildContext context) {
    final p = Palette.of(context);
    return ListenableBuilder(
      listenable: widget.controller,
      builder: (context, _) {
        final c = widget.controller;
        final list = _showAll ? c.bundles : c.bundles.take(3).toList();
        return Card(
          key: const ValueKey('diag-bundle'),
          child: Padding(
            padding: const EdgeInsets.all(14),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'Collect diagnostics',
                  style: Theme.of(context).textTheme.titleMedium,
                ),
                const SizedBox(height: 4),
                Text(
                  'Pulls the terminal\'s bus recording, sentence log and '
                  'device list over the boat Wi-Fi, adds this phone\'s relay '
                  'log, and saves one zip you can share. The app also saves '
                  'one by itself when the bus goes wrong.',
                  style: TextStyle(color: p.faint),
                ),
                const SizedBox(height: 10),
                if (c.busy) ...[
                  LinearProgressIndicator(
                    key: const ValueKey('diag-bundle-progress'),
                    value: c.progress,
                  ),
                  const SizedBox(height: 6),
                  Text(c.step),
                  const SizedBox(height: 6),
                ],
                if (c.error != null && !c.busy) ...[
                  Text(
                    c.error!,
                    key: const ValueKey('diag-bundle-error'),
                    style: TextStyle(
                      color: Theme.of(context).colorScheme.error,
                    ),
                  ),
                  const SizedBox(height: 6),
                ],
                Wrap(
                  spacing: 10,
                  runSpacing: 10,
                  children: [
                    FilledButton.icon(
                      key: const ValueKey('diag-collect'),
                      onPressed: c.busy
                          ? null
                          : () => unawaited(_collect('manual')),
                      icon: const Icon(Icons.archive_outlined),
                      label: const Text('Collect diagnostics'),
                    ),
                    FilledButton.tonalIcon(
                      key: const ValueKey('diag-mark'),
                      onPressed: c.busy ? null : () => unawaited(_mark()),
                      icon: const Icon(Icons.flag_outlined),
                      label: const Text('Mark problem'),
                    ),
                  ],
                ),
                if (list.isNotEmpty) ...[
                  const SizedBox(height: 8),
                  for (final f in list)
                    ListTile(
                      contentPadding: EdgeInsets.zero,
                      dense: true,
                      leading: const Icon(Icons.folder_zip_outlined),
                      title: Text(_name(f.path)),
                      trailing: IconButton(
                        tooltip: 'Share',
                        icon: const Icon(Icons.ios_share),
                        onPressed: () => unawaited(_share(f.path)),
                      ),
                    ),
                  if (c.bundles.length > 3)
                    TextButton(
                      onPressed: () => setState(() => _showAll = !_showAll),
                      child: Text(
                        _showAll
                            ? 'Show fewer'
                            : 'Show all ${c.bundles.length}',
                      ),
                    ),
                ],
              ],
            ),
          ),
        );
      },
    );
  }
}
