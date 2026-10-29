# Play Store: signing, bundles and release

App: **LMYC Boat Terminal**, applicationId **`ca.lmyc.boat`** (registered in
Play Console). The Kotlin namespace stays `ca.lmyc.lmyc_app` and the Dart
package `lmyc_app`; only the installed id changed. A phone that had the old
`ca.lmyc.lmyc_app` build keeps it as a separate app: uninstall it once.
The app's files (relay log, diagnostics zips) now live in
`/sdcard/Android/data/ca.lmyc.boat/files/`.

## 1. Upload key (once)

Use Play App Signing: Google holds the app signing key, you keep an
**upload key**. Create it outside the repo:

```sh
mkdir -p ~/.android-keys/lmyc-boat && cd ~/.android-keys/lmyc-boat
keytool -genkeypair -v -keystore upload.jks -alias upload \
  -keyalg RSA -keysize 4096 -validity 10000
```

Then `~/.android-keys/lmyc-boat/key.properties`:

```properties
storeFile=upload.jks          # relative to this file, or an absolute path
storePassword=<STORE_PASSWORD>
keyAlias=upload
keyPassword=<KEY_PASSWORD>
```

Back up both files (password manager / offline). If the upload key is lost
it can be reset through Play support, but it takes days.

`mobile/android/app/build.gradle.kts` reads `$LMYC_KEY_PROPERTIES` if set,
else the path above. If neither exists it prints a WARNING and signs the
release with the debug key, which Play rejects. `key.properties`, `*.jks`
and `*.keystore` are gitignored.

## 2. Version

`mobile/pubspec.yaml`: `version: 0.3.0+3` is versionName `0.3.0`,
versionCode `3`. **Every upload needs a higher versionCode** (the `+N`),
even to a testing track; bump versionName as you like. Play remembers every
code ever uploaded, including rejected ones.

## 3. Build

```sh
make phone-bundle
# → mobile/build/app/outputs/bundle/release/app-release.aab
```

Check it is signed with the upload key, not debug:

```sh
keytool -printcert -jarfile mobile/build/app/outputs/bundle/release/app-release.aab
```

(The owner should be your upload certificate, not `CN=Android Debug`.)

## 4. Upload and testing

1. Play Console > LMYC Boat Terminal > Testing > **Closed testing** > create a track.
2. Upload `app-release.aab`, add release notes, roll out to the track.
3. New personal developer accounts must run a closed test with **at least
   12 testers opted in for 14 consecutive days** before applying for
   production access. Add them by email list or Google Group; each opens the
   opt-in link and installs from Play. Keep them opted in for the full 14
   days (count restarts if it drops below 12).
4. Then Production > Apply for access, answer the questionnaire, and roll out.
   Internal testing (up to 100, no review wait) is handy for quick builds but
   does not count toward the 12/14 rule.

## 5. Permissions and why (for the store listing / review)

| Permission | Why |
|---|---|
| BLUETOOTH_SCAN (neverForLocation), BLUETOOTH_CONNECT; BLUETOOTH/ADMIN ≤ API 30 | find and pair with the boat terminal over BLE, read its stats |
| ACCESS_FINE/COARSE_LOCATION | BLE scanning on older Android; optional phone-GPS position sent to the terminal |
| NEARBY_WIFI_DEVICES (neverForLocation), ACCESS/CHANGE_WIFI_STATE, CHANGE_NETWORK_STATE, ACCESS_NETWORK_STATE | join the terminal's own hotspot for this app only (WifiNetworkSpecifier) and watch it |
| INTERNET | local TCP/HTTP to the terminal (192.168.4.1) and the loopback relay for chart apps; maintenance sync |
| FOREGROUND_SERVICE, FOREGROUND_SERVICE_CONNECTED_DEVICE | "Connected to the boat": the relay keeps serving Navionics/iBoating on 127.0.0.1:10110 while they are in front. Play asks for a justification and a short video of this service type |
| POST_NOTIFICATIONS | the required notification of that service |
| CAMERA | scan the pairing QR code on the terminal; maintenance photos |
| READ_MEDIA_IMAGES | attach existing photos to maintenance items |

The location permission triggers the Play location declaration if used in
the background; the app only uses it in the foreground, say so.

## 6. Data safety form

- **Collected / shared with us**: nothing sent to LMYC or third-party servers
  by the app itself. Boat data stays between the phone and the terminal on
  the boat's own Wi-Fi.
- **Location**: precise location is optionally sent to the boat terminal on the
  local network (not off the device to a server). Declare "Location: approximate
  and precise, app functionality, optional" if Play counts the local device as
  sharing; it is not stored by us.
- **Photos**: maintenance photos go to the boat terminal only.
- **Diagnostics zips**: stored on the phone; shared only when the user taps
  Share (user-initiated, so not "sharing" for the form). They contain boat
  position history from the bus and the phone's relay log; mention this in the
  privacy policy.
- Data is not encrypted in transit on the boat LAN (plain HTTP/TCP to the terminal).
- No account, no ads, no analytics SDK. Users can delete data by clearing app storage.
- A **privacy policy URL** is required (location and camera permissions): host a
  short page on the LMYC site saying the above.
