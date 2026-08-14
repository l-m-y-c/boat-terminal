# LMYC Mobile App

Flutter companion app for the Lower Mainland Yacht Club boat terminal.

## Purpose

- Open from `lmyc://` deep links (QR code on the boat terminal)
- Scan and connect over BLE to the matching terminal
- Foundation for secure pairing, check-in, and boat tools

See `../docs/06-phone-app-and-pairing.md` and `../docs/08-user-flows.md`.

## Current status (v0.2)

- Android `lmyc://` deep-link handler
- Parses real terminal QR payload (`boat`, `tid`, `ble`, `oob`, `v`)
- In-app BLE scan targeting the exact name from the QR
- GATT connect + read of the LMYC payload characteristic
- Clear status UI (permissions → scanning → connecting → connected)

**Note:** Android’s system Bluetooth Settings often hides pure LE peripherals. Use the in-app **Connect to terminal** button — it is more reliable than Settings → Scan.

## Development

```bash
cd mobile
flutter pub get
flutter run
```

### Test deep link without QR

```bash
adb shell am start -a android.intent.action.VIEW \
  -d "lmyc://pair?v=1&boat=BENCH-01&tid=WS7-001&ble=LMYC-D649&oob=test"
```

Then tap **Connect to terminal** (with the real board advertising under that BLE name).

## Permissions

On first connect the app requests:

- `BLUETOOTH_SCAN` / `BLUETOOTH_CONNECT` (Android 12+)
- Location (only required for BLE scan on Android 11 and older)

## Next steps

1. Use OOB data for LE Secure Connections (replace Just Works)
2. Booking-token exchange after GATT connect
3. “Paired” state mirrored on the terminal UI
4. Simple log / engine-hour forms once paired
