import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

/// This boat's maintenance book, read from the terminal TF card over SoftAP.
/// No cell service and no club login. See docs/18.
class MaintenanceBookPage extends StatefulWidget {
  const MaintenanceBookPage({super.key, this.suggestedIp});

  final String? suggestedIp;

  @override
  State<MaintenanceBookPage> createState() => _MaintenanceBookPageState();
}

class _MaintenanceBookPageState extends State<MaintenanceBookPage> {
  static const _categories = <String>[
    'engine',
    'electrical',
    'hull',
    'rigging',
    'plumbing',
    'interior',
    'safety-equipment',
    'sails',
    'cleanliness',
    'other',
  ];
  static const _priorities = <String>['low', 'medium', 'high', 'critical'];

  final _ipController = TextEditingController();
  final _titleController = TextEditingController();
  final _descController = TextEditingController();
  String _category = 'other';
  String _priority = 'medium';
  bool _busy = false;
  String _status = '';
  List<Map<String, dynamic>> _items = [];

  @override
  void initState() {
    super.initState();
    _ipController.text = widget.suggestedIp?.isNotEmpty == true
        ? widget.suggestedIp!
        : '192.168.4.1';
  }

  @override
  void dispose() {
    _ipController.dispose();
    _titleController.dispose();
    _descController.dispose();
    super.dispose();
  }

  String get _host => _ipController.text.trim();

  Future<void> _refresh() async {
    setState(() {
      _busy = true;
      _status = 'Reading the boat…';
    });
    try {
      final res = await http
          .get(Uri.http(_host, '/maintenance'))
          .timeout(const Duration(seconds: 8));
      if (res.statusCode != 200) {
        throw Exception('HTTP ${res.statusCode}');
      }
      final decoded = jsonDecode(res.body);
      if (decoded is! List) {
        throw Exception('Expected a list');
      }
      setState(() {
        _items = decoded.whereType<Map<String, dynamic>>().toList();
        _status = '${_items.length} on this boat';
      });
    } catch (e) {
      setState(() => _status = 'Could not read the book ($e)');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _file() async {
    final title = _titleController.text.trim();
    if (title.isEmpty) {
      setState(() => _status = 'A summary is required');
      return;
    }
    setState(() {
      _busy = true;
      _status = 'Saving on the boat…';
    });
    try {
      final res = await http
          .post(
            Uri.http(_host, '/maintenance'),
            headers: {'Content-Type': 'application/json'},
            body: jsonEncode({
              'title': title,
              'description': _descController.text.trim(),
              'category': _category,
              'priority': _priority,
            }),
          )
          .timeout(const Duration(seconds: 8));
      if (res.statusCode != 200) {
        throw Exception('HTTP ${res.statusCode} ${res.body}');
      }
      _titleController.clear();
      _descController.clear();
      await _refresh();
    } catch (e) {
      setState(() => _status = 'Save failed ($e)');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Boat maintenance book')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          const Text(
            'Join the terminal Wi-Fi, then read the copy stored on the boat. '
            'This does not post to lmyc.ca.',
            style: TextStyle(height: 1.4),
          ),
          const SizedBox(height: 12),
          TextField(
            controller: _ipController,
            decoration: const InputDecoration(
              labelText: 'Terminal IP',
              border: OutlineInputBorder(),
            ),
            keyboardType: TextInputType.url,
          ),
          const SizedBox(height: 8),
          FilledButton.icon(
            onPressed: _busy ? null : _refresh,
            icon: const Icon(Icons.refresh),
            label: const Text('Read book'),
          ),
          if (_status.isNotEmpty) ...[
            const SizedBox(height: 8),
            Text(_status),
          ],
          const SizedBox(height: 16),
          if (_items.isEmpty)
            const Text('Nothing loaded yet.')
          else
            ..._items.map(_tile),
          const Divider(height: 32),
          const Text(
            'File on this boat',
            style: TextStyle(fontWeight: FontWeight.w600, fontSize: 16),
          ),
          const SizedBox(height: 8),
          TextField(
            controller: _titleController,
            maxLength: 200,
            decoration: const InputDecoration(
              labelText: 'Issue summary',
              border: OutlineInputBorder(),
            ),
          ),
          const SizedBox(height: 8),
          DropdownButtonFormField<String>(
            initialValue: _category,
            decoration: const InputDecoration(
              labelText: 'Category',
              border: OutlineInputBorder(),
            ),
            items: [
              for (final c in _categories)
                DropdownMenuItem(value: c, child: Text(c)),
            ],
            onChanged: _busy
                ? null
                : (v) {
                    if (v != null) setState(() => _category = v);
                  },
          ),
          const SizedBox(height: 8),
          DropdownButtonFormField<String>(
            initialValue: _priority,
            decoration: const InputDecoration(
              labelText: 'Priority',
              border: OutlineInputBorder(),
            ),
            items: [
              for (final p in _priorities)
                DropdownMenuItem(value: p, child: Text(p)),
            ],
            onChanged: _busy
                ? null
                : (v) {
                    if (v != null) setState(() => _priority = v);
                  },
          ),
          const SizedBox(height: 8),
          TextField(
            controller: _descController,
            minLines: 3,
            maxLines: 6,
            maxLength: 2000,
            decoration: const InputDecoration(
              labelText: 'Details',
              border: OutlineInputBorder(),
            ),
          ),
          const SizedBox(height: 8),
          FilledButton.icon(
            onPressed: _busy ? null : _file,
            icon: const Icon(Icons.save_alt),
            label: const Text('Save on the boat'),
          ),
        ],
      ),
    );
  }

  Widget _tile(Map<String, dynamic> item) {
    final title = item['title']?.toString() ?? '(no title)';
    final category = item['category']?.toString() ?? '';
    final priority = item['priority']?.toString() ?? '';
    final status = item['status']?.toString() ?? '';
    final sync = item['sync']?.toString() ?? '';
    final photos = item['photo_count']?.toString() ?? '0';
    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      child: ListTile(
        title: Text(title),
        subtitle: Text('$category · $priority · $status · $sync · $photos photo(s)'),
      ),
    );
  }
}
