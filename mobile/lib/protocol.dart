/// The terminal <-> phone contract. `firmware/src/protocol.h` and
/// `tools/maint_card_server.py` carry the same values, and
/// `tools/check_protocol.py` fails CI when they drift. Change all three.
library;

// BLE GATT.
const kLmycServiceUuid = '6c6d7963-0001-4000-8000-000000000001';
const kLmycPayloadUuid = '6c6d7963-0001-4000-8000-000000000010';
const kLmycSessionUuid = '6c6d7963-0001-4000-8000-000000000020';
const kLmycStatusUuid = '6c6d7963-0001-4000-8000-000000000021';
const kLmycNmeaUuid = '6c6d7963-0001-4000-8000-000000000030';
const kLmycWifiUuid = '6c6d7963-0001-4000-8000-000000000040';

/// Data source and bus statistics, JSON, refreshed once a second.
const kLmycStatsUuid = '6c6d7963-0001-4000-8000-000000000050';

/// Advertised name is this prefix, the boat ID, and the last two MAC bytes in hex.
const kLmycBlePrefix = 'LMYC-';

// Wi-Fi.
const kSoftApIp = '192.168.4.1';
const kGatewayTcpPort = 10110;
const kGatewayUdpPort = 10110;
const kGatewayUdpAltPort = 2000;

// Maintenance book. Text limits are UTF-8 bytes, not characters: the card
// stores fixed byte buffers.
const kMaintTitleMax = 200;
const kMaintDescMax = 2000;
const kMaintReporterMax = 80;
const kMaintPhotoMax = 5;
const kMaintPhotoBytesMax = 500 * 1024;
const kMaintPhotoNoteMax = 120;

/// Largest JSON body for POST /maintenance and /maintenance/reconcile.
const kMaintBodyMax = 8192;

const kMaintCategories = <String>[
  'cleanliness',
  'electrical',
  'engine',
  'hull',
  'interior',
  'other',
  'plumbing',
  'rigging',
  'safety-equipment',
  'sails',
];
const kMaintPriorities = <String>['critical', 'high', 'medium', 'low'];
const kMaintStatuses = <String>[
  'reported',
  'in-progress',
  'scheduled',
  'completed',
  'future-wishlist',
];
