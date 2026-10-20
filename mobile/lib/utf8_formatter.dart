import 'dart:convert';

import 'package:flutter/services.dart';

import 'utf8_limit.dart';

/// Stops input once the field would pass [maxBytes] of UTF-8.
class Utf8LengthLimitingFormatter extends TextInputFormatter {
  Utf8LengthLimitingFormatter(this.maxBytes);

  final int maxBytes;

  @override
  TextEditingValue formatEditUpdate(
    TextEditingValue oldValue,
    TextEditingValue newValue,
  ) {
    if (utf8.encode(newValue.text).length <= maxBytes) {
      return newValue;
    }
    // A paste that does not fit keeps as much as fits.
    final clipped = clipUtf8(newValue.text, maxBytes);
    return TextEditingValue(
      text: clipped,
      selection: TextSelection.collapsed(offset: clipped.length),
    );
  }
}
