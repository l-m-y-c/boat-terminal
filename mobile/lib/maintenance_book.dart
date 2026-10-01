import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'package:image_picker/image_picker.dart';
import 'package:permission_handler/permission_handler.dart';

import 'maintenance_api.dart';
import 'maintenance_jpeg.dart';

/// This boat's maintenance book over the terminal Wi-Fi, or a stand-in on a
/// computer. Photos are stored with the note. The club field stays blank until
/// lmyc.ca has the JSON route, and a blank field does not call the website.
class MaintenanceBookPage extends StatefulWidget {
  const MaintenanceBookPage({super.key, this.suggestedIp, this.client});

  final String? suggestedIp;

  /// Injected in tests. The page closes a client it creates itself.
  final http.Client? client;

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
  final _clubController = TextEditingController();
  final _titleController = TextEditingController();
  final _descController = TextEditingController();
  final _picker = ImagePicker();
  final List<Uint8List> _photos = [];

  String _category = 'other';
  String _priority = 'medium';
  bool _busy = false;
  String _status = '';
  List<Map<String, dynamic>> _items = [];
  late final http.Client _client;
  late final bool _ownsClient;

  @override
  void initState() {
    super.initState();
    _ipController.text = widget.suggestedIp?.isNotEmpty == true
        ? widget.suggestedIp!
        : '192.168.4.1';
    _ownsClient = widget.client == null;
    _client = widget.client ?? http.Client();
  }

  @override
  void dispose() {
    _ipController.dispose();
    _clubController.dispose();
    _titleController.dispose();
    _descController.dispose();
    if (_ownsClient) {
      _client.close();
    }
    super.dispose();
  }

  void _report(String message) {
    setState(() => _status = message);
    ScaffoldMessenger.of(context).hideCurrentSnackBar();
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(message)));
  }

  Future<void> _refresh({String? done}) async {
    final host = terminalHost(_ipController.text);
    if (host.isEmpty) {
      setState(() => _status = 'Enter the terminal address');
      return;
    }
    setState(() {
      _busy = true;
      _status = 'Reading the boat…';
    });
    try {
      final items = await TerminalBook(host: host, client: _client).list();
      if (!mounted) return;
      setState(() {
        _items = items;
        _status = done ?? '${items.length} on this boat';
      });
      if (done != null) {
        ScaffoldMessenger.of(context).hideCurrentSnackBar();
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(done)));
      }
    } catch (e) {
      if (!mounted) return;
      _report('Could not read the book ($e)');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _addPhoto(ImageSource source) async {
    if (_photos.length >= 5) {
      setState(() => _status = 'Five photos is the limit');
      return;
    }
    if (source == ImageSource.camera) {
      final allowed = await Permission.camera.request();
      if (!allowed.isGranted) {
        if (mounted) {
          setState(() => _status = 'Camera permission is off');
        }
        return;
      }
    }
    final picked = await _picker.pickImage(
      source: source,
      maxWidth: 2000,
      maxHeight: 2000,
      imageQuality: 80,
    );
    if (picked == null || !mounted) return;
    setState(() {
      _busy = true;
      _status = 'Preparing photo…';
    });
    try {
      final fitted = fitMaintenanceJpeg(await picked.readAsBytes());
      if (!mounted) return;
      setState(() {
        _photos.add(fitted);
        _status = '${_photos.length} of 5 photos ready';
      });
    } catch (e) {
      if (!mounted) return;
      setState(() => _status = '$e');
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
    final host = terminalHost(_ipController.text);
    if (host.isEmpty) {
      setState(() => _status = 'Enter the terminal address');
      return;
    }
    setState(() {
      _busy = true;
      _status = 'Saving on the boat…';
    });
    final book = TerminalBook(host: host, client: _client);
    final queued = List<Uint8List>.from(_photos);
    try {
      final id = await book.create(
        title: title,
        description: _descController.text.trim(),
        category: _category,
        priority: _priority,
      );
      var sent = 0;
      Object? photoError;
      for (var i = 0; i < queued.length; i++) {
        try {
          await book.putPhoto(id, i + 1, queued[i]);
          sent++;
        } catch (e) {
          photoError = e;
          break;
        }
      }
      _titleController.clear();
      _descController.clear();
      _photos.clear();
      final note = photoError == null
          ? 'Saved on the boat'
                '${sent == 0 ? '' : ' with $sent photo(s)'}. '
                'Club copy stays pending.'
          : 'The note is on the boat with $sent of ${queued.length} photos. '
                '$photoError';
      await _refresh(done: note);
    } catch (e) {
      if (!mounted) return;
      _report('Save failed ($e)');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _uploadClub() async {
    final club = ClubMaintenanceClient(
      rawBaseUrl: _clubController.text,
      client: _client,
    );
    if (!club.configured) {
      _report(ClubMaintenanceClient.offMessage);
      return;
    }
    final host = terminalHost(_ipController.text);
    if (host.isEmpty) {
      setState(() => _status = 'Enter the terminal address');
      return;
    }
    setState(() {
      _busy = true;
      _status = 'Syncing with the club…';
    });
    try {
      final book = TerminalBook(host: host, client: _client);
      final result = await club.syncBook(book);
      if (!mounted) return;
      await _refresh(done: result.message);
    } catch (e) {
      if (!mounted) return;
      _report('Club upload stopped ($e)');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  void _openItem(Map<String, dynamic> item) {
    final count = int.tryParse('${item['photo_count']}') ?? 0;
    final id = item['id']?.toString() ?? '';
    if (count <= 0 || id.isEmpty) return;
    final host = terminalHost(_ipController.text);
    if (host.isEmpty) return;
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => _BoatPhotoPage(
          book: TerminalBook(host: host, client: _client),
          item: item,
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Boat maintenance book')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          const Text(
            'Join the terminal Wi-Fi, or type this computer’s stand-in address '
            '(host and port, no http://). Photos are saved on the boat. '
            'Leave Club API blank and the note stays pending. '
            'Sync with club sends pending notes and brings home edits back.',
            style: TextStyle(height: 1.4),
          ),
          const SizedBox(height: 12),
          TextField(
            key: const Key('terminal-host'),
            controller: _ipController,
            decoration: const InputDecoration(
              labelText: 'Terminal IP',
              hintText: '192.168.4.1',
              border: OutlineInputBorder(),
            ),
            keyboardType: TextInputType.url,
          ),
          const SizedBox(height: 8),
          TextField(
            key: const Key('club-api'),
            controller: _clubController,
            decoration: const InputDecoration(
              labelText: 'Club API',
              hintText: 'Blank until lmyc.ca has the JSON route',
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
          const SizedBox(height: 8),
          OutlinedButton.icon(
            onPressed: _busy ? null : _uploadClub,
            icon: const Icon(Icons.cloud_upload_outlined),
            label: const Text('Sync with club'),
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
            key: const Key('maint-title'),
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
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              OutlinedButton.icon(
                onPressed: _busy ? null : () => _addPhoto(ImageSource.camera),
                icon: const Icon(Icons.photo_camera_outlined),
                label: const Text('Camera'),
              ),
              OutlinedButton.icon(
                onPressed: _busy ? null : () => _addPhoto(ImageSource.gallery),
                icon: const Icon(Icons.photo_library_outlined),
                label: const Text('Gallery'),
              ),
            ],
          ),
          if (_photos.isNotEmpty) ...[
            const SizedBox(height: 8),
            SizedBox(
              height: 88,
              child: ListView.separated(
                scrollDirection: Axis.horizontal,
                itemCount: _photos.length,
                separatorBuilder: (_, _) => const SizedBox(width: 8),
                itemBuilder: (context, i) {
                  return Stack(
                    children: [
                      ClipRRect(
                        borderRadius: BorderRadius.circular(8),
                        child: Image.memory(
                          _photos[i],
                          width: 88,
                          height: 88,
                          fit: BoxFit.cover,
                        ),
                      ),
                      Positioned(
                        right: 0,
                        top: 0,
                        child: IconButton(
                          onPressed: _busy
                              ? null
                              : () => setState(() => _photos.removeAt(i)),
                          icon: const Icon(Icons.close, size: 18),
                          style: IconButton.styleFrom(
                            backgroundColor: Colors.black54,
                            foregroundColor: Colors.white,
                          ),
                        ),
                      ),
                    ],
                  );
                },
              ),
            ),
          ],
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
    final photos = int.tryParse('${item['photo_count']}') ?? 0;
    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      child: ListTile(
        title: Text(title),
        subtitle: Text(
          '$category · $priority · $status · $sync · $photos photo(s)',
        ),
        trailing: photos > 0 ? const Icon(Icons.photo_outlined) : null,
        onTap: photos > 0 ? () => _openItem(item) : null,
      ),
    );
  }
}

class _BoatPhotoPage extends StatefulWidget {
  const _BoatPhotoPage({required this.book, required this.item});

  final TerminalBook book;
  final Map<String, dynamic> item;

  @override
  State<_BoatPhotoPage> createState() => _BoatPhotoPageState();
}

class _BoatPhotoPageState extends State<_BoatPhotoPage> {
  int _n = 1;
  Uint8List? _bytes;
  String _error = '';
  bool _busy = false;

  int get _count => int.tryParse('${widget.item['photo_count']}') ?? 0;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() {
      _busy = true;
      _error = '';
    });
    try {
      final bytes = await widget.book.getPhoto(
        widget.item['id'].toString(),
        _n,
      );
      if (!mounted) return;
      setState(() => _bytes = bytes);
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _bytes = null;
        _error = 'Could not load photo $_n ($e)';
      });
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  void _step(int delta) {
    final next = _n + delta;
    if (next < 1 || next > _count || _busy) return;
    setState(() => _n = next);
    _load();
  }

  @override
  Widget build(BuildContext context) {
    final title = widget.item['title']?.toString() ?? 'Photo';
    return Scaffold(
      backgroundColor: Colors.black,
      appBar: AppBar(title: Text(title)),
      body: Column(
        children: [
          Expanded(
            child: Center(
              child: _busy
                  ? const CircularProgressIndicator()
                  : _bytes != null
                  ? InteractiveViewer(
                      child: Image.memory(_bytes!, fit: BoxFit.contain),
                    )
                  : Padding(
                      padding: const EdgeInsets.all(24),
                      child: Text(
                        _error,
                        style: const TextStyle(color: Colors.white),
                        textAlign: TextAlign.center,
                      ),
                    ),
            ),
          ),
          if (_count > 1)
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 0, 16, 24),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  TextButton(
                    onPressed: _n > 1 && !_busy ? () => _step(-1) : null,
                    child: const Text('Previous'),
                  ),
                  Text(
                    '$_n of $_count',
                    style: const TextStyle(color: Colors.white),
                  ),
                  TextButton(
                    onPressed: _n < _count && !_busy ? () => _step(1) : null,
                    child: const Text('Next'),
                  ),
                ],
              ),
            ),
        ],
      ),
    );
  }
}
