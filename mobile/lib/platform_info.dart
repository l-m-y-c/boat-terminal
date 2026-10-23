import 'dart:io' show Platform;

import 'package:flutter/services.dart';

const _channel = MethodChannel('ca.lmyc/platform');
bool? _copyToast;

/// True on Android 13+ (API 33), where the system confirms clipboard copies.
Future<bool> systemShowsCopyToast() async {
  final known = _copyToast;
  if (known != null) return known;
  var shows = false;
  if (Platform.isAndroid) {
    try {
      final sdk = await _channel.invokeMethod<int>('sdkInt');
      shows = (sdk ?? 0) >= 33;
    } catch (_) {}
  }
  return _copyToast = shows;
}
