#!/usr/bin/env python3
"""Stand-in for the Waveshare maintenance book and the future club JSON route.

The phone on the same Wi-Fi uses this computer instead of the board:

    python3 tools/maint_card_server.py

Terminal IP on the phone is the printed host and port, with no http://.
Leave Club API blank to keep items pending. Paste the printed http URL into
Club API to walk the upload and the ack.

Nothing here contacts lmyc.ca. The store is a temp directory and is removed
when the server stops.
"""

from __future__ import annotations

import json
import os
import re
import secrets
import shutil
import socket
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

# The terminal <-> phone contract. firmware/src/protocol.h and
# mobile/lib/protocol.dart carry the same values, and tools/check_protocol.py
# fails CI when they drift. Change all three.
CATEGORIES = {
    "cleanliness",
    "electrical",
    "engine",
    "hull",
    "interior",
    "other",
    "plumbing",
    "rigging",
    "safety-equipment",
    "sails",
}
PRIORITIES = {"critical", "high", "medium", "low"}
STATUSES = {
    "reported",
    "in-progress",
    "scheduled",
    "completed",
    "future-wishlist",
}
PHOTO_MAX = 500 * 1024
PHOTO_COUNT_MAX = 5
# Text limits are UTF-8 bytes, like the card's fixed buffers.
TITLE_MAX = 200
DESC_MAX = 2000
REPORTER_MAX = 80
NOTE_MAX = 120
BODY_MAX = 8192
ACK_BODY_MAX = 256
SAILBOAT_ID = int(os.environ.get("LMYC_SAILBOAT_ID", "0") or "0")
BOAT = os.environ.get("LMYC_BOAT_ID", "BENCH-01")



def clip_utf8(text: str, limit: int) -> str:
    """Longest prefix within limit UTF-8 bytes, cut on a character boundary.

    The board truncates by bytes. Slicing characters here let a long accented
    note pass on the stand-in and come back shorter from the boat.
    """
    return text.encode("utf-8")[:limit].decode("utf-8", "ignore")


ROOT = tempfile.mkdtemp(prefix="lmyc-maint-")
LOCK = threading.Lock()
NEXT_WP = 1000
WP_BY_LOCAL: dict[str, int] = {}
CLUB: dict[int, dict] = {}


def _now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def _item_dir(item_id: str) -> str:
    return os.path.join(ROOT, item_id)


def _item_path(item_id: str) -> str:
    return os.path.join(_item_dir(item_id), "item.json")


def _load(item_id: str) -> dict | None:
    path = _item_path(item_id)
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def _save(item: dict) -> None:
    folder = _item_dir(item["id"])
    os.makedirs(folder, exist_ok=True)
    tmp = os.path.join(folder, "item.tmp")
    path = _item_path(item["id"])
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(item, fh)
        fh.write("\n")
    os.replace(tmp, path)


def _lan_ips() -> list[str]:
    found: set[str] = set()
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.connect(("8.8.8.8", 80))
        found.add(sock.getsockname()[0])
        sock.close()
    except OSError:
        pass
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            found.add(info[4][0])
    except OSError:
        pass
    found.discard("127.0.0.1")
    return sorted(found) or ["127.0.0.1"]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def _send(self, code: int, body: bytes, content_type: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _json(self, code: int, payload: object) -> None:
        self._send(code, json.dumps(payload).encode("utf-8"), "application/json")

    def _read(self, limit: int) -> bytes | None:
        raw = self.headers.get("Content-Length", "0")
        try:
            length = int(raw)
        except ValueError:
            return None
        if length < 0 or length > limit:
            return None
        return self.rfile.read(length)

    def _query(self) -> dict[str, str]:
        parsed = parse_qs(urlparse(self.path).query)
        return {key: values[0] for key, values in parsed.items() if values}

    def do_GET(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        if path == "/":
            self._send(
                200,
                b"LMYC maintenance stand-in. GET /maintenance\n",
                "text/plain; charset=utf-8",
            )
            return
        if path == "/status.json":
            self._json(200, {"boat": BOAT, "sailboat_id": SAILBOAT_ID})
            return
        club = re.fullmatch(r"/wp-json/lmyc/v1/boats/(\d+)/maintenance", path)
        if club:
            sailboat_id = int(club.group(1))
            with LOCK:
                rows = [
                    dict(row)
                    for row in CLUB.values()
                    if int(row["sailboat_id"]) == sailboat_id
                ]
            rows.sort(key=lambda row: int(row["wp_id"]))
            self._json(200, rows)
            return
        if path == "/maintenance":
            with LOCK:
                rows = []
                for name in sorted(os.listdir(ROOT)):
                    item = _load(name)
                    if item is not None:
                        rows.append(item)
            self._json(200, rows)
            return
        if path == "/maintenance/item":
            item_id = self._query().get("id", "")
            with LOCK:
                item = _load(item_id)
            if item is None:
                self._json(404, {"error": "no item"})
                return
            self._json(200, item)
            return
        if path == "/maintenance/photo":
            query = self._query()
            try:
                n = int(query.get("n", "0"))
            except ValueError:
                n = 0
            photo = os.path.join(_item_dir(query.get("id", "")), f"{n}.jpg")
            if n < 1 or n > PHOTO_COUNT_MAX or not os.path.isfile(photo):
                self._json(404, {"error": "no photo"})
                return
            with open(photo, "rb") as fh:
                self._send(200, fh.read(), "image/jpeg")
            return
        self._json(404, {"error": "not found"})

    def do_POST(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        club = re.fullmatch(r"/wp-json/lmyc/v1/boats/(\d+)/maintenance", path)
        if path == "/maintenance":
            self._post_item()
        elif path == "/maintenance/photo":
            self._post_photo()
        elif path == "/maintenance/ack":
            self._post_ack()
        elif path == "/maintenance/reconcile":
            self._post_reconcile()
        elif club:
            self._post_club(int(club.group(1)))
        else:
            self._json(404, {"error": "not found"})

    def do_PUT(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        club = re.fullmatch(
            r"/wp-json/lmyc/v1/boats/(\d+)/maintenance/(\d+)", path
        )
        if not club:
            self._json(404, {"error": "not found"})
            return
        self._put_club(int(club.group(1)), int(club.group(2)))

    def _post_item(self) -> None:
        raw = self._read(BODY_MAX)
        if raw is None:
            self._json(400, {"error": "body size"})
            return
        try:
            body = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self._json(400, {"error": "json"})
            return
        title = str(body.get("title", "")).strip()
        category = str(body.get("category", ""))
        priority = str(body.get("priority", ""))
        if not title or category not in CATEGORIES or priority not in PRIORITIES:
            self._json(400, {"error": "title, category, priority"})
            return
        stamp = _now()
        item = {
            "id": secrets.token_hex(8),
            "wp_id": 0,
            "sailboat_id": SAILBOAT_ID,
            "boat": BOAT,
            "title": clip_utf8(title, TITLE_MAX),
            "description": clip_utf8(str(body.get("description", "")), DESC_MAX),
            "category": category,
            "priority": priority,
            "status": "reported",
            "reporter": "",
            "created": stamp,
            "modified": stamp,
            "sync": "pending",
            "photo_count": 0,
        }
        with LOCK:
            _save(item)
        self._json(200, {"id": item["id"], "sync": "pending"})

    def _post_photo(self) -> None:
        query = self._query()
        item_id = query.get("id", "")
        try:
            n = int(query.get("n", "0"))
        except ValueError:
            n = 0
        blob = self._read(PHOTO_MAX)
        if n < 1 or n > PHOTO_COUNT_MAX or blob is None or len(blob) < 32:
            self._json(400, {"error": "photo size or index"})
            return
        with LOCK:
            item = _load(item_id)
            if item is None:
                self._json(404, {"error": "no item"})
                return
            folder = _item_dir(item_id)
            tmp = os.path.join(folder, f"{n}.tmp")
            dest = os.path.join(folder, f"{n}.jpg")
            with open(tmp, "wb") as fh:
                fh.write(blob)
            os.replace(tmp, dest)
            item["photo_count"] = max(int(item.get("photo_count", 0)), n)
            note = query.get("note", "").replace("\x00", "").strip()
            note = clip_utf8(note, NOTE_MAX)
            if note:
                item[f"note{n}"] = note
            item["modified"] = _now()
            _save(item)
        self._send(200, b"OK\n", "text/plain; charset=utf-8")

    def _post_ack(self) -> None:
        item_id = self._query().get("id", "")
        raw = self._read(ACK_BODY_MAX)
        if raw is None:
            self._json(400, {"error": "body"})
            return
        try:
            body = json.loads(raw.decode("utf-8"))
            wp_id = int(body.get("wp_id", 0))
        except (UnicodeDecodeError, json.JSONDecodeError, TypeError, ValueError):
            self._json(400, {"error": "wp_id"})
            return
        if wp_id <= 0:
            self._json(400, {"error": "wp_id"})
            return
        with LOCK:
            item = _load(item_id)
            if item is None:
                self._json(404, {"error": "no item"})
                return
            item["wp_id"] = wp_id
            item["sync"] = "synced"
            _save(item)
        self._json(200, {"id": item_id, "wp_id": wp_id, "sync": "synced"})

    def _post_club(self, sailboat_id: int) -> None:
        raw = self._read(BODY_MAX)
        if raw is None:
            self._json(400, {"error": "body size"})
            return
        try:
            body = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self._json(400, {"error": "json"})
            return
        local_id = str(body.get("local_id", "")).strip()
        if not local_id:
            self._json(400, {"error": "local_id"})
            return
        global NEXT_WP
        stamp = _now()
        with LOCK:
            wp_id = WP_BY_LOCAL.get(local_id)
            if wp_id is None:
                wp_id = NEXT_WP
                NEXT_WP += 1
                WP_BY_LOCAL[local_id] = wp_id
                CLUB[wp_id] = {
                    "wp_id": wp_id,
                    "local_id": local_id,
                    "sailboat_id": sailboat_id,
                    "title": clip_utf8(str(body.get("title", "")), TITLE_MAX),
                    "description": clip_utf8(str(body.get("description", "")), DESC_MAX),
                    "category": str(body.get("category", "")),
                    "priority": str(body.get("priority", "")),
                    "status": "reported",
                    "reporter": "",
                    "created": stamp,
                    "modified": stamp,
                }
        self._json(201, {"wp_id": wp_id, "sailboat_id": sailboat_id, "local_id": local_id})

    def _put_club(self, sailboat_id: int, wp_id: int) -> None:
        raw = self._read(BODY_MAX)
        if raw is None:
            self._json(400, {"error": "body size"})
            return
        try:
            body = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self._json(400, {"error": "json"})
            return
        with LOCK:
            row = CLUB.get(wp_id)
            if row is None or int(row["sailboat_id"]) != sailboat_id:
                self._json(404, {"error": "no item"})
                return
            for key in ("title", "description", "category", "priority", "status", "reporter"):
                if key in body:
                    row[key] = str(body[key])
            row["modified"] = str(body.get("modified") or _now())
            stored = dict(row)
        self._json(200, stored)

    def _post_reconcile(self) -> None:
        raw = self._read(BODY_MAX)
        if raw is None:
            self._json(400, {"error": "body size"})
            return
        try:
            body = json.loads(raw.decode("utf-8"))
            wp_id = int(body.get("wp_id", 0))
        except (UnicodeDecodeError, json.JSONDecodeError, TypeError, ValueError):
            self._json(400, {"error": "json"})
            return
        title = str(body.get("title", "")).strip()
        category = str(body.get("category", ""))
        priority = str(body.get("priority", ""))
        status = str(body.get("status", "") or "reported")
        if wp_id <= 0 or not title or category not in CATEGORIES or priority not in PRIORITIES:
            self._json(400, {"error": "wp_id, title, category, priority"})
            return
        if status not in STATUSES:
            self._json(400, {"error": "status"})
            return
        modified = str(body.get("modified", ""))
        with LOCK:
            found = None
            for name in os.listdir(ROOT):
                item = _load(name)
                if item is not None and int(item.get("wp_id", 0)) == wp_id:
                    found = item
                    break
            if found is None:
                stamp = modified or _now()
                item = {
                    "id": secrets.token_hex(8),
                    "wp_id": wp_id,
                    "sailboat_id": int(body.get("sailboat_id") or SAILBOAT_ID),
                    "boat": BOAT,
                    "title": clip_utf8(title, TITLE_MAX),
                    "description": clip_utf8(str(body.get("description", "")), DESC_MAX),
                    "category": category,
                    "priority": priority,
                    "status": status,
                    "reporter": clip_utf8(str(body.get("reporter", "")), REPORTER_MAX),
                    "created": str(body.get("created") or stamp),
                    "modified": stamp,
                    "sync": "synced",
                    "photo_count": 0,
                }
                _save(item)
                action = "added"
                item_id = item["id"]
            elif found.get("sync") == "pending":
                action = "skipped"
                item_id = found["id"]
            elif not modified or modified <= str(found.get("modified", "")):
                action = "kept"
                item_id = found["id"]
            else:
                found["title"] = clip_utf8(title, TITLE_MAX)
                found["description"] = clip_utf8(str(body.get("description", "")), DESC_MAX)
                found["category"] = category
                found["priority"] = priority
                found["status"] = status
                found["reporter"] = clip_utf8(str(body.get("reporter", "")), REPORTER_MAX)
                found["modified"] = modified
                found["sync"] = "synced"
                _save(found)
                action = "updated"
                item_id = found["id"]
        self._json(200, {"action": action, "id": item_id, "wp_id": wp_id})


def main() -> None:
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    ips = _lan_ips()
    print("LMYC maintenance stand-in")
    print(f"  store: {ROOT}")
    print(f"  sailboat_id: {SAILBOAT_ID}")
    for ip in ips:
        print(f"  phone Terminal IP: {ip}:{port}")
        print(f"  phone Club API:    http://{ip}:{port}")
    print("Leave Club API blank to keep the copy pending.")
    print("This does not contact lmyc.ca.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        server.server_close()
        shutil.rmtree(ROOT, ignore_errors=True)


if __name__ == "__main__":
    main()
