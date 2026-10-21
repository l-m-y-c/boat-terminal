#pragma once

#include <cstddef>
#include <cstdint>

/* The terminal <-> phone contract. mobile/lib/protocol.dart and
 * tools/maint_card_server.py carry the same values, and
 * tools/check_protocol.py fails CI when they drift. Change all three. */

/* BLE GATT. */
static constexpr const char *kLmycServiceUuid = "6c6d7963-0001-4000-8000-000000000001";
static constexpr const char *kLmycPayloadUuid = "6c6d7963-0001-4000-8000-000000000010";
static constexpr const char *kLmycSessionUuid = "6c6d7963-0001-4000-8000-000000000020";
/* Read/notify only — never written by the phone (avoids Android write-cache garbage). */
static constexpr const char *kLmycStatusUuid = "6c6d7963-0001-4000-8000-000000000021";
/* One NMEA sentence per notify. Drop on BLE if longer than ATT payload. */
static constexpr const char *kLmycNmeaUuid = "6c6d7963-0001-4000-8000-000000000030";
/* SoftAP bootstrap. Readable only after OOB confirmation. */
static constexpr const char *kLmycWifiUuid = "6c6d7963-0001-4000-8000-000000000040";
/* Data source and bus statistics, JSON, refreshed once a second. Read only. */
static constexpr const char *kLmycStatsUuid = "6c6d7963-0001-4000-8000-000000000050";

/* Advertised name is this prefix, the boat ID, and the last two MAC bytes in hex. */
static constexpr const char *kLmycBlePrefix = "LMYC-";

/* Wi-Fi. */
static constexpr const char *kSoftApIp = "192.168.4.1";
static constexpr uint16_t kGatewayTcpPort = 10110;
static constexpr uint16_t kGatewayUdpPort = 10110;
static constexpr uint16_t kGatewayUdpAltPort = 2000;

/* Maintenance book. Text limits are UTF-8 bytes, not characters. */
static constexpr size_t kMaintTitleMax = 200;
static constexpr size_t kMaintDescMax = 2000;
static constexpr size_t kMaintReporterMax = 80;
static constexpr size_t kMaintPhotoMax = 5;
static constexpr size_t kMaintPhotoBytesMax = 500 * 1024;
static constexpr size_t kMaintPhotoNoteMax = 120;
/* Largest JSON body for POST /maintenance and /maintenance/reconcile. */
static constexpr size_t kMaintBodyMax = 8192;

static constexpr const char *kMaintCategories[] = {
    "cleanliness", "electrical", "engine", "hull", "interior",
    "other", "plumbing", "rigging", "safety-equipment", "sails",
};
static constexpr const char *kMaintPriorities[] = {"critical", "high", "medium", "low"};
static constexpr const char *kMaintStatuses[] = {
    "reported", "in-progress", "scheduled", "completed", "future-wishlist",
};
