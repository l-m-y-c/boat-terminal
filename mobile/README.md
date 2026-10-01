# LMYC Mobile App

Flutter companion app for the Lower Mainland Yacht Club boat terminal.

## Purpose

- Open from `lmyc://` deep links (QR code on the boat terminal)
- Scan and connect over BLE to the matching terminal
- Foundation for secure pairing, check-in, and boat tools

See `../docs/06-phone-app-and-pairing.md` and `../docs/08-user-flows.md`.

## Current status (v0.3)

- Android `lmyc://` deep-link handler **and** a paste-URI field (camera apps often don't open custom schemes)
- **Find terminals** — in-app BLE scan that does **not** require a QR first
- Matches `LMYC-*` advertised name (name is in the primary ADV packet) plus the LMYC service UUID if present
- Shows heard-device count vs LMYC hits (diagnostics)
- GATT connect + raw 16-byte OOB write (from QR hex) once a QR / URI is applied
  — fits default ATT MTU 23; the old `PAIR <hex>` text write was 37B and died on Android
- After pair: **Sentences** console (BLE `…000030` notify + optional TCP `:10110`)
- **Maintenance book** reads and files items, including up to five JPEGs, on the terminal TF card. Club API stays blank until lmyc.ca has the JSON route; a blank field leaves every item `pending`.
- Clear status UI (permissions → scanning → connecting → paired)

**Do not use Android Settings → Bluetooth.** System Settings hide most LE-only peripherals. Use **Find terminals**.

## Development

```bash
cd mobile
flutter pub get
flutter run
```

Or from the repo root: `make phone-reinstall` after firmware/BLE changes.

### Test deep link without QR

```bash
adb shell am start -a android.intent.action.VIEW \
  -d "lmyc://pair?v=1&boat=BENCH-01&tid=WS7-001&ble=LMYC-D649&oob=0123456789abcdef0123456789abcdef"
```

`oob` must be 32 hex chars (16 raw bytes). Copy the live URI off the terminal serial log (`Pairing payload: lmyc://...`) — OOB rotates every boot.

### Without the Waveshare

The board can stay at the shop. From the repo root, on a computer joined to the same Wi-Fi as the phone:

```bash
python3 tools/maint_card_server.py
```

In the app, open **Maintenance book** on the home screen (pairing can wait). Set **Terminal IP** to the printed `host:port` (no `http://`). File a note, attach a photo, and tap the row to see it. Leave **Club API** blank to confirm the copy stays pending. Paste the printed `http://…` URL into **Club API** and tap **Sync with club**. Pending notes go up, and a newer club edit comes back onto the card. The stand-in does not contact lmyc.ca.

## Permissions

On first connect the app requests:

- `BLUETOOTH_SCAN` / `BLUETOOTH_CONNECT` (Android 12+)
- Location (only required for BLE scan on Android 11 and older)
- Camera, when a maintenance photo is taken in the app

## Next steps

1. Use OOB data for LE Secure Connections (replace Just Works)
2. Booking-token exchange after GATT connect
3. Member tools once paired
