import 'dart:typed_data';

import 'package:image/image.dart' as img;

/// Long edge the terminal can scale onto the 800×480 panel.
const int kMaintLongEdge = 2000;

/// Matches firmware `kMaintPhotoBytesMax`.
const int kMaintPhotoBytesMax = 500 * 1024;

/// Decode [bytes], keep aspect, and return a JPEG the card will accept.
///
/// Images already at or under [kMaintLongEdge] are not enlarged. Quality
/// starts at 80 and steps down to 40 until the file is at most 500 KB.
Uint8List fitMaintenanceJpeg(Uint8List bytes) {
  final decoded = img.decodeImage(bytes);
  if (decoded == null) {
    throw const FormatException('Could not read that photo');
  }
  var picture = decoded;
  final long = picture.width > picture.height ? picture.width : picture.height;
  if (long > kMaintLongEdge) {
    if (picture.width >= picture.height) {
      final height = (picture.height * kMaintLongEdge / picture.width).round();
      picture = img.copyResize(
        picture,
        width: kMaintLongEdge,
        height: height < 1 ? 1 : height,
        interpolation: img.Interpolation.linear,
      );
    } else {
      final width = (picture.width * kMaintLongEdge / picture.height).round();
      picture = img.copyResize(
        picture,
        width: width < 1 ? 1 : width,
        height: kMaintLongEdge,
        interpolation: img.Interpolation.linear,
      );
    }
  }
  var quality = 80;
  var jpg = img.encodeJpg(picture, quality: quality);
  while (jpg.length > kMaintPhotoBytesMax && quality > 40) {
    quality -= 10;
    jpg = img.encodeJpg(picture, quality: quality);
  }
  if (jpg.length > kMaintPhotoBytesMax) {
    throw StateError('Photo is still larger than 500 KB');
  }
  return jpg;
}
