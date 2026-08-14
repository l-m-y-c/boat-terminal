# LMYC Mobile App

Flutter companion app for the Lower Mainland Yacht Club boat terminal.

## Purpose

- Respond to `lmyc://` deep links (opened from the QR code on the boat terminal)
- Support secure pairing between member phone and on-boat terminal
- Provide check-in / booking context and simple tools (log notes, engine hours, etc.)

See `../docs/06-phone-app-and-pairing.md` and `../docs/08-user-flows.md` for the full design.

## Current status (v0.1)

- Android deep-link handler for the `lmyc://` scheme is registered
- App shows the most recent deep-link payload (boat id + pairing token when present)
- Clean Material 3 UI with LMYC navy branding
- Ready for the next increment: real pairing handshake, BLE, and booking integration

## Development

```bash
cd mobile

# Get dependencies
flutter pub get

# Run on connected Android device (S26 Ultra, etc.)
flutter run

# Build release APK
flutter build apk
```

### Testing the deep link

With the app installed, you can simulate a QR scan from a computer:

```bash
adb shell am start -a android.intent.action.VIEW -d "lmyc://pair?token=test123&boat=deserata"
```

Or create a simple QR code containing:

```
lmyc://pair?token=test123&boat=deserata
```

and scan it with the phone camera / Google Lens.

## Project layout

```
mobile/
├── lib/
│   └── main.dart          # App entry + deep-link handling + home screen
├── android/               # Android project (intent-filter for lmyc://)
├── pubspec.yaml
└── README.md
```

## Next steps

1. Add a dedicated Pairing screen that is pushed when a valid `lmyc://pair` link arrives
2. Implement BLE / secure handshake with the boat terminal (see firmware pairing code)
3. Hook into the existing LMYC web booking system for member identity and current reservations
4. Add simple observation / engine-hour forms
