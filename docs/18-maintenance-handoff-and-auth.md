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
- the photos, as JPEGs, beside the item, each with an optional note of up to 120 characters (`note1` … `note5` in `item.json`)
- a sync cursor, and which items were created on board and still need uploading

Completed items stay. "We replaced this pump, here is the old one" is part of the handoff. One boat's history is small next to a large card. If a card ever fills, drop the oldest completed photos first and keep the text.

The phone converts each photo to JPEG and limits the long edge to about 2000 pixels before writing the card. That is enough for the 800×480 screen and for zooming on the phone. The ESP32 does not decode HEIC.

Layout (FAT32; exFAT will not mount on this firmware):

```
/maint/<id>/item.json
/maint/<id>/1.jpg
```

`item.json` carries `id`, `wp_id` (0 until the website accepts it), `sailboat_id`, `title`, `description`, `category`, `priority`, `status`, `reporter`, `created`, `modified`, `sync` (`pending` or `synced`), `photos`, and `note1` … `note5` (the line that belongs to `1.jpg` … `5.jpg`, for example "Sail is ripped, left hand side"). Writes go to a temporary file and are renamed into place after they are flushed, so an engine-start voltage dip does not tear the record.

### Who reads it

The book is on the Waveshare with no phone and no coverage. Open reports are the list. A row opens that item. A photo opens on the whole glass, and a right swipe moves between that picture and its note. A left swipe opens the rail. On the item, a right swipe opens the next report and wraps. **Back** returns to the item. The screen is the glance.

### How a report is laid out

The list holds every open report on one screen (six at most). A header names the columns: Title, Category, Priority. A camera mark in its own column means that report has photos. The open items are read from the card the first time Reports opens and kept in RAM until the phone writes the book. Each photo is still decoded from the card when that report is opened. Each row is one line, and each field has its own place. A long title or category ends in an ellipsis. The category is the word (`safety-equipment` is shown as Safety equipment), because ten small pictograms are harder to learn at arm's length than a word that always sits in the same column. Priority is a chip with the word on it: Critical and High are amber, Medium is teal, Low is gray. Color and the word travel together, so the chip still reads when the color is hard to see.

The detail page is a set of wells in the card color. The field name is small and gray. The value is white. Category, priority, status, and reporter sit in a two-by-two band under the title, and priority uses the same chip as the list. The description has its own well and shows about four lines. The page is the only scroll on that screen, so a scrollbar inside a field does not fight the page. Photos below stay reachable.

When the title or the description is longer than its well, tap it. A reader covers the detail page, shows the full title and the full description, and scrolls. **Close** returns to the same report, at the same place in the page.

Photos stack down the page. The picture takes half the width and keeps its shape, with black bars where the frame is wider or taller than the photo. The note fills a well of the same height on the other half. A long note is clipped there. The full note is the Photo screen. Tap the picture to open Photo on the picture. Tap the note to open Photo already on the note. A right swipe rotates between the two. A left swipe opens the rail. On the detail page a right swipe opens the next report and wraps back to the first. The reader stays on its report. An empty note still has that screen: "No note for this photo".

Reviewing several problems is better on the phone, which reads the same files over the terminal's SoftAP. After the pair, the phone learns that network's name and passphrase over BLE and Android joins it. Photos move over that Wi-Fi. Bluetooth stays the pairing handshake.

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

1. On-card store and local HTTP. In this repo: `firmware/src/maint_store.*`, `GET`/`POST /maintenance`, `GET`/`POST /maintenance/photo`, and `POST /maintenance/ack`. The ack route writes `wp_id` and sets `sync` to `synced` after the club JSON API accepts the item. The terminal still does not call lmyc.ca.
2. Phone screen reads that list and files a pending item with up to five JPEGs (long edge about 2000 px, under 500 KB) onto the card. A blank Club API field does not open a connection, so the items stay `pending`. `tools/maint_card_server.py` speaks the same routes on a computer, for a phone test while the Waveshare is not on the bench.
3. Waveshare list of open reports, then the item, then one photo large enough to recognize the part. **Reports** on the right-hand rail opens the list. The rail parks off the glass behind a tab; tap the tab or swipe left to bring the buttons out. **Home** in the top bar always returns to the landing screen. The path there is **Reports**, then **Details**, then **Photo**. **Reports** returns to the list and **Details** returns to the open report. The list is Title, Category, and a priority chip. The item is labeled wells, and a long title or description opens a reader on that same page. Each photo is half the width, with its note in a well beside it (the phone sends that note on `POST /maintenance/photo?id=&n=&note=`). The picture opens Photo on the picture. The note opens Photo on the note. Swipe right between them. Swipe right on the item for the next report. Swipe left opens the rail. **Back** on the chart rail returns to the item and **Next photo** steps through that item, back on the picture. The chart page and a book photo still share one canvas. See `firmware/README.md`.
4. Bearer-token client for those JSON routes. The phone uploads `sync: pending` items, then downloads club edits. `POST /maintenance/reconcile` writes an edit onto the card when the website `modified` time is newer, and leaves a still-pending boat note alone. A blank Club API field does not open a connection. The live lmyc.ca routes are still waiting on the site owner. The stand-in answers both directions.
5. Boarding pass check in the existing QR + BLE handshake.

**lmyc.ca** (the existing maintenance PHP, not a new site)

1. Opaque-token table, authorize + token endpoints, PKCE.
2. The JSON routes that call the current submit and table functions.
3. Boarding-pass issuer.

Instrument work (real RS485 listen, isolation, N2K) continues on [doc 17](17-android-boat-roadmap.md) and still waits on the Board items in [doc 12](12-next-steps.md) before anyone writes to a live bus.
