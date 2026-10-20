import 'dart:convert';

/// Longest prefix of [text] that is at most [maxBytes] as UTF-8, cut on a
/// character boundary. The terminal stores fixed byte buffers, so a limit
/// counted in characters lets accented or non-Latin text overflow.
String clipUtf8(String text, int maxBytes) {
  if (utf8.encode(text).length <= maxBytes) {
    return text;
  }
  var used = 0;
  final out = StringBuffer();
  for (final rune in text.runes) {
    final n = rune < 0x80
        ? 1
        : rune < 0x800
        ? 2
        : rune < 0x10000
        ? 3
        : 4;
    if (used + n > maxBytes) {
      break;
    }
    used += n;
    out.writeCharCode(rune);
  }
  return out.toString();
}
