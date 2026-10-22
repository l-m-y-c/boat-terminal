# LMYC Mobile App

Flutter companion app for the Lower Mainland Yacht Club boat terminal.

## Purpose

- Open from `lmyc://` deep links (QR code on the boat terminal)
- Scan and connect over BLE to the matching terminal
- Foundation for secure pairing, check-in, and boat tools

See `../docs/06-phone-app-and-pairing.md` and `../docs/08-user-flows.md`.

## Current status (v0.3)

- **One guided journey** on one screen: **Find → Connect → Wi-Fi → NMEA**, the same four stops the terminal shows. One title, one sentence, and at most one main button per stop; it moves on by itself. See [doc 06](../docs/06-phone-app-and-pairing.md#the-journey).
- **Find**: **Scan QR code** first, **Find nearby boats** second. A boat joined before is found and joined after a five-second countdown.
- **Wi-Fi**: the relay joins the boat network for the LMYC app alone. Android asks **Connect** once, then remembers; nothing is saved on the phone. The app checks the link every two seconds and when it comes back to the front.
- **NMEA**: a foreground service, **Connected to the boat**, holds the boat Wi-Fi and repeats the terminal's stream on `127.0.0.1` (TCP 10110, and UDP datagrams to 10110 and 2000) (`android/app/src/main/kotlin/ca/lmyc/lmyc_app/BoatRelayService.kt`). Navionics and i-Boating use **UDP port 10110**, address `127.0.0.1` where asked, so mobile data stays on.
- **On board**: the data-source pill, **Connect a chart app**, **Boat data** (BLE `…000050` counters), and **Leave the boat**.
- **Diagnostics** behind the settings icon: link rows, Bluetooth detail, the **Sentences** console, Find a terminal.
- The maintenance book and chart snapshot pages are still in the code but not reachable, matching the terminal's default build.
- Android `lmyc://` deep links and a paste-URI field; GATT connect and the raw 16-byte OOB write.

**Do not use Android Settings → Bluetooth.** System Settings hide most LE-only peripherals. Use the app.

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
