# 18 – Maintenance handoff, login, and the boat's copy

**Status:** agreed 2026-09-30. This is the member-facing roadmap. Instrument work stays in [17 – BoatLink](17-android-boat-roadmap.md).
**Supersedes,** for the maintenance book only: the "short queue, no photos, forget on power loss" rule in [09 – Security](09-security-and-privacy.md). Credentials still never live on the terminal. Instruments still work with no phone and no pairing.

---

## What we are building

Members asked for one seamless loop across three things:

- the **phone app**, which is the member, the camera, and the network
- the **Waveshare 7″**, which is the boat's bright touch screen and its on-board record
- **lmyc.ca**, which is the club's copy

The **13.3″ E-Ink is a separate project.** It shows pictures the phone sends it. It is not part of login, pairing, or the maintenance book.

The sweet spot is a shared handoff store on the boat. A member can arrive with no cell coverage, pair (or just look at the screen), and read this boat's open problems, including the photos. The website catches up when some later phone has a signal.

## Two identities

| Who | Where the session lives | What it may do |
|-----|-------------------------|----------------|
| **Member** | Phone keystore, and the website cookie | File and read maintenance as that person. Book, when their MemberPress role allows it. |
| **Boat terminal** | Provisioned once, bound to one sailboat id | Hold this boat's maintenance book. Accept a paired member's writes. Never store a member refresh token. |

A cabin screen shared by the whole co-op is the wrong place for a login that can act as a person. While someone is paired, the terminal may show their display name and when the pairing ends. When they leave, that session is gone. The boat's book stays.

## Login

One member account: the WordPress user lmyc.ca already has, with the MemberPress role it already has (full member, social member, maintenance leader, and the rest).

The phone uses **OAuth 2.0 authorization code with PKCE**.

1. The app opens lmyc.ca in the system browser, not in a password field inside the app.
2. The member signs in with the MemberPress form at `/login/`.
3. The browser returns to `lmyc://callback` with a one-time code.
4. The app exchanges the code and the PKCE verifier for a short access token and a rotating refresh token.
5. Maintenance and, later, bookings go out as `Authorization: Bearer …`.

Tokens are opaque random strings in a WordPress table, next to the user id. A lost phone or a departing member is one deleted row. Every request loads that user and checks the MemberPress role. The token means "this is the same person." The role check means "this person may still do this." Access tokens last about an hour.

The website keeps its cookie login. It does not log in to itself through OAuth. Same user table, two doors.

lmyc.ca is the authorization server. A separate identity provider would be a second user directory beside MemberPress (roles, courses, payments, volunteer hours). That is a poor trade for a volunteer co-op. Revisit it only if WordPress stops being the membership system.

The phone fetches a **boarding pass** while it still has coverage: a signed, short-lived proof for this member and this boat. Pairing later, in a bay with no signal, is the pass plus the QR and BLE handshake the firmware already does. The terminal checks the pass locally.

Reading the maintenance book on the Waveshare does not wait for a pass. The book is the boat's. The pass is what lets a new note be attributed to the member.

## The website as it is today

lmyc.ca is WordPress with MemberPress. These facts were read from the live site while logged in as a member. They are the contract the app must match until the JSON API below replaces the nonce dance.

Maintenance reports are already a custom post type, `maintenance_item`. The form on `/maintenance-submit/` posts multipart to `admin-ajax.php` with `action=lmyc_submit_maintenance`.

| Form field | Wire name | Values |
|------------|-----------|--------|
| Sailboat | `sailboat_id` | Baca Lele 276, Benesse 308, Chili Pepper 505, Deserata 2919, Scherzando 504, Teachers Pet 506 |
| Category | `category` | `cleanliness`, `electrical`, `engine`, `hull`, `interior`, `other`, `plumbing`, `rigging`, `safety-equipment`, `sails` |
| Priority | `priority` | `critical`, `high`, `medium`, `low` |
| Summary | `title` | required, 200 characters |
| Description | `description` | optional |
| Photos | `photos[]` | up to 5, 5 MB each |

The member list at `/maintenance-items-table-view/` loads through `action=lmyc_get_maintenance_table_data`, filtered by `boat_id`. A row includes `id`, `boat_id`, `boat_name`, `title`, `description`, `category`, `priority`, `status_slug`, dates, and reporter. Status slugs in use are `reported`, `in-progress`, `scheduled`, `completed`, `future-wishlist`. Maintenance leaders also see assigned-to, scheduled date, completed date, estimated cost, parts needed, and resolution notes. A normal member may see a thinner row.

The public REST route `GET /wp-json/wp/v2/maintenance_item` is readable without a login and does **not** include boat or priority. The app uses the club API below, not that public route. Bookings are a second ajax app on the same login (`lmycBooking` on `/sailboat-booking/`). Same account, later slice.

Creating an item anonymously on the public REST API returns 401. The real write path is the ajax action, and it requires a logged-in cookie plus a nonce printed on the page. That nonce is why the phone needs a proper token API.

## What changes on lmyc.ca

Keep MemberPress, courses, bookings, and volunteer hours. Add a JSON API beside the PHP that already implements `lmyc_submit_maintenance` and `lmyc_get_maintenance_table_data`. Those functions stay the implementation. The new routes are the door the phone uses.

- authorize and token endpoints for the phone (authorization code + PKCE)
- `GET /wp-json/lmyc/v1/me`
- `GET /wp-json/lmyc/v1/boats/{sailboat_id}/maintenance`
- `POST /wp-json/lmyc/v1/boats/{sailboat_id}/maintenance` with the same fields as the form
- `POST` a boarding pass for the paired terminal

The current website form can keep posting to admin-ajax until it calls the same functions. Members at home see no change.

Boat id on an item is already stored (the table returns `boat_id`). The public REST schema hides it. The new routes return it.

## The card is the handoff store

The Waveshare TF slot holds **this sailboat's** maintenance book, photos included. Internal flash is firmware. The card is the record.

On the card, for this boat only:

- every item: title, description, category, priority, status, dates, reporter display name, and leader notes once those exist
- the photos, as JPEGs, beside the item
- a sync cursor, and which items were created on board and still need uploading

Completed items stay. "We replaced this pump, here is the old one" is part of the handoff. One boat's history is small next to a large card. If a card ever fills, drop the oldest completed photos first and keep the text.

The phone converts each photo to JPEG and limits the long edge to about 2000 pixels before writing the card. That is enough for the 800×480 screen and for zooming on the phone. The ESP32 does not decode HEIC.

Layout (FAT32; exFAT will not mount on this firmware):

```
/maint/<id>/item.json
/maint/<id>/1.jpg
```

`item.json` carries `id`, `wp_id` (0 until the website accepts it), `sailboat_id`, `title`, `description`, `category`, `priority`, `status`, `reporter`, `created`, `modified`, `sync` (`pending` or `synced`), and `photos`. Writes go to a temporary file and are renamed into place after they are flushed, so an engine-start voltage dip does not tear the record.

### Who reads it

The book is on the Waveshare with no phone and no coverage. Open items first, then the photo large enough to recognize the part. The screen is the glance.

Reviewing several problems is better on the phone, which reads the same files over the terminal's SoftAP. Photos move over that Wi-Fi. Bluetooth stays the pairing handshake.

### Who carries it to the club

The phone is the courier, when it is paired and has a path to lmyc.ca.

- Items closed or edited at home replace the copy on the card. The website's modified time wins for those fields.
- Items created on the boat upload with their photos. The card then stores the WordPress id so a retry does not file them twice.
- Photos match by hash, so the same picture is not stored again.

Only this boat's id is synced. Scherzando's terminal does not carry Benesse's book.

The terminal does not post to lmyc.ca itself. It often has no route off the boat.

## What stays off the card

Member passwords, OAuth refresh tokens, and other people's bookings stay off the card. A gate code or a starting note can join the handoff later if the crew wants that on the boat. That is a separate decision from the maintenance photos.

The terminal still must not advertise occupancy or a member name in BLE advertising. See [09](09-security-and-privacy.md).

## Build order

**This repo**

1. On-card store and local HTTP read, so a phone on the SoftAP can list this boat's book with no cell service.
2. Phone screen to read that list and file a pending item, photos included, onto the card.
3. Waveshare list of open items and one photo at a time.
4. Bearer-token client, once lmyc.ca has the routes above, including upload of `sync: pending` items and download of club-side edits.
5. Boarding pass check in the existing QR + BLE handshake.

**lmyc.ca** (the existing maintenance PHP, not a new site)

1. Opaque-token table, authorize + token endpoints, PKCE.
2. The JSON routes that call the current submit and table functions.
3. Boarding-pass issuer.

Instrument work (real RS485 listen, isolation, N2K) continues on [doc 17](17-android-boat-roadmap.md) and still waits on the Board items in [doc 12](12-next-steps.md) before anyone writes to a live bus.
