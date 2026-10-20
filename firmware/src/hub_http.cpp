#include "hub_http.h"

#include "inject.h"
#include "json_lite.h"
#include "maint_store.h"
#include "nmea.h"
#include "pairing.h"
#include "snapshot.h"
#include "gateway.h"

#include <SD.h>

#include <Arduino.h>
#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include <freertos/portmacro.h>
#include <cstring>

static httpd_handle_t g_httpd = nullptr;
static char g_last_err[48] = "";
static uint32_t g_last_snap_ms = 0;

const char *hub_http_last_error(void) { return g_last_err; }

static constexpr int kHttpLog = 6;
static char g_http_log[kHttpLog][72];
static int g_http_log_n = 0;
static portMUX_TYPE g_http_mux = portMUX_INITIALIZER_UNLOCKED;

static const char *method_name(int method)
{
    switch (method) {
    case HTTP_GET:
        return "GET";
    case HTTP_POST:
        return "POST";
    case HTTP_PUT:
        return "PUT";
    case HTTP_DELETE:
        return "DELETE";
    case HTTP_HEAD:
        return "HEAD";
    default:
        return "HTTP";
    }
}

static int http_push(httpd_req_t *req)
{
    char line[72];
    const char *uri = (req != nullptr && req->uri != nullptr) ? req->uri : "/";
    snprintf(line, sizeof(line), "%s %s", method_name(req != nullptr ? req->method : 0), uri);
    char query[40];
    if (req != nullptr &&
        httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        const size_t used = strlen(line);
        snprintf(line + used, sizeof(line) - used, "?%s", query);
    }
    portENTER_CRITICAL(&g_http_mux);
    int slot;
    if (g_http_log_n < kHttpLog) {
        slot = g_http_log_n;
        g_http_log_n++;
    } else {
        memmove(g_http_log[0], g_http_log[1], (kHttpLog - 1) * sizeof(g_http_log[0]));
        slot = kHttpLog - 1;
    }
    snprintf(g_http_log[slot], sizeof(g_http_log[0]), "%s", line);
    portEXIT_CRITICAL(&g_http_mux);
    Serial.printf("HTTP %s\n", line);
    return slot;
}

static void http_status(int slot, int code)
{
    if (slot < 0) {
        return;
    }
    portENTER_CRITICAL(&g_http_mux);
    if (slot < g_http_log_n) {
        char *line = g_http_log[slot];
        const size_t used = strlen(line);
        snprintf(line + used, sizeof(g_http_log[0]) - used, "  %d", code);
    }
    portEXIT_CRITICAL(&g_http_mux);
}

struct HttpTrace {
    int slot;
    explicit HttpTrace(httpd_req_t *req) : slot(http_push(req)) {}
    esp_err_t fail(httpd_req_t *req, httpd_err_code_t code, const char *msg)
    {
        http_status(slot, static_cast<int>(code));
        httpd_resp_send_err(req, code, msg);
        return ESP_FAIL;
    }
    esp_err_t fail500(httpd_req_t *req)
    {
        http_status(slot, 500);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    esp_err_t ok(esp_err_t err)
    {
        http_status(slot, err == ESP_OK ? 200 : 500);
        return err;
    }
};

bool hub_http_listening(void) { return g_httpd != nullptr; }

void hub_http_copy_last(char *out, size_t n)
{
    if (out == nullptr || n == 0) {
        return;
    }
    portENTER_CRITICAL(&g_http_mux);
    if (g_http_log_n == 0) {
        out[0] = '\0';
    } else {
        snprintf(out, n, "%s", g_http_log[g_http_log_n - 1]);
    }
    portEXIT_CRITICAL(&g_http_mux);
}

void hub_http_copy_log(char *out, size_t n)
{
    if (out == nullptr || n == 0) {
        return;
    }
    char snap[kHttpLog][72];
    int count = 0;
    portENTER_CRITICAL(&g_http_mux);
    count = g_http_log_n;
    if (count > 0) {
        memcpy(snap, g_http_log, sizeof(g_http_log));
    }
    portEXIT_CRITICAL(&g_http_mux);
    out[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < count && used + 1 < n; ++i) {
        const int wrote = snprintf(out + used, n - used, "%s%s",
                                    i == 0 ? "" : "\n", snap[i]);
        if (wrote < 0 || static_cast<size_t>(wrote) >= n - used) {
            break;
        }
        used += static_cast<size_t>(wrote);
    }
}

static const char kIndexHtml[] =
    "<!doctype html><html><head><meta charset=utf-8><meta name=viewport "
    "content='width=device-width,initial-scale=1'><title>LMYC BoatLink</title>"
    "<style>body{font-family:sans-serif;background:#071422;color:#e8f0f7;"
    "max-width:40rem;margin:2rem auto;padding:0 1rem}code{background:#12243a;"
    "padding:.15rem .4rem;border-radius:4px}h1{color:#00b4a6}</style></head>"
    "<body><h1>LMYC BoatLink</h1>"
    "<p>NMEA 0183 gateway for Navionics / OpenCPN.</p>"
    "<ol><li>Join Wi-Fi <code>LMYC-" LMYC_BOAT_ID "</code></li>"
    "<li>Navionics → Menu → Connected Devices → Add device</li>"
    "<li>TCP <code>192.168.4.1:10110</code> (or UDP 10110 / 2000)</li></ol>"
    "<p>POST JPEG to <code>/snapshot</code> (800×480, ≤220 KB). "
    "POST NMEA to <code>/inject</code> only when the 7″ shows GNSS LOCAL.</p>"
    "<p>This boat's maintenance book is <code>GET /maintenance</code>. "
    "It is on the card and does not need cell service.</p>"
    "</body></html>";

static esp_err_t get_root(httpd_req_t *req)
{
    HttpTrace trace(req);
    httpd_resp_set_type(req, "text/html");
    return trace.ok(httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN));
}

static esp_err_t get_status(httpd_req_t *req)
{
    HttpTrace trace(req);
    char json[320];
    snprintf(json, sizeof(json),
             "{\"boat\":\"%s\",\"sailboat_id\":%lu,\"tcp\":%lu,\"nmea_good\":%lu,\"nmea_bad\":%lu,"
             "\"inject_local\":%s,\"snapshot\":%s}\n",
             LMYC_BOAT_ID,
             static_cast<unsigned long>(LMYC_SAILBOAT_ID),
             static_cast<unsigned long>(gateway_client_count()),
             static_cast<unsigned long>(nmea_good_count()),
             static_cast<unsigned long>(nmea_bad_count()),
             inject_local_armed() ? "true" : "false",
             snapshot_has_image() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return trace.ok(httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN));
}

static esp_err_t post_snapshot(httpd_req_t *req)
{
    HttpTrace trace(req);
    const size_t n = req->content_len;
    if (n == 0 || n > kSnapshotJpegMax) {
        snprintf(g_last_err, sizeof(g_last_err), "bad jpeg size %u",
                 static_cast<unsigned>(n));
        return trace.fail(req, HTTPD_400_BAD_REQUEST, "jpeg too large");
    }
    const uint32_t now = millis();
    if (g_last_snap_ms != 0 && (now - g_last_snap_ms) < 1000) {
        return trace.fail(req, HTTPD_400_BAD_REQUEST, "rate 1 fps");
    }

    uint8_t *buf = static_cast<uint8_t *>(
        heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buf == nullptr) {
        buf = static_cast<uint8_t *>(malloc(n));
    }
    if (buf == nullptr) {
        return trace.fail500(req);
    }

    size_t got = 0;
    while (got < n) {
        const int r = httpd_req_recv(req, reinterpret_cast<char *>(buf + got), n - got);
        if (r <= 0) {
            free(buf);
            return trace.fail(req, HTTPD_408_REQ_TIMEOUT, "recv");
        }
        got += static_cast<size_t>(r);
    }

    const bool ok = snapshot_load_jpeg(buf, n);
    free(buf);
    if (!ok) {
        snprintf(g_last_err, sizeof(g_last_err), "decode failed");
        return trace.fail(req, HTTPD_400_BAD_REQUEST, "decode failed");
    }
    g_last_snap_ms = now;
    httpd_resp_set_type(req, "text/plain");
    return trace.ok(httpd_resp_sendstr(req, "OK\n"));
}

static esp_err_t post_inject(httpd_req_t *req)
{
    HttpTrace trace(req);
    if (!inject_local_armed()) {
        return trace.fail(req, HTTPD_400_BAD_REQUEST, "LOCAL not armed on 7 inch");
    }
    char body[512];
    const int n = httpd_req_recv(req, body, sizeof(body) - 1);
    if (n <= 0) {
        return trace.fail(req, HTTPD_400_BAD_REQUEST, "empty");
    }
    body[n] = '\0';
    char *p = body;
    int accepted = 0;
    while (*p != '\0') {
        char *eol = strpbrk(p, "\r\n");
        if (eol != nullptr) {
            *eol = '\0';
        }
        if (p[0] != '\0' && inject_is_gnss_sentence(p) && inject_submit(p)) {
            ++accepted;
        }
        if (eol == nullptr) {
            break;
        }
        p = eol + 1;
        while (*p == '\r' || *p == '\n') {
            ++p;
        }
    }
    char resp[32];
    snprintf(resp, sizeof(resp), "OK %d\n", accepted);
    httpd_resp_set_type(req, "text/plain");
    return trace.ok(httpd_resp_sendstr(req, resp));
}

static bool query_id(httpd_req_t *req, char *id, size_t cap)
{
    char query[80];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    if (httpd_query_key_value(query, "id", id, cap) != ESP_OK) {
        return false;
    }
    return maint_id_ok(id);
}

static esp_err_t send_sd_file(httpd_req_t *req, const char *path)
{
    File in = SD.open(path, FILE_READ);
    if (!in) {
        return ESP_FAIL;
    }
    char buf[512];
    while (in.available()) {
        const int n = in.read(reinterpret_cast<uint8_t *>(buf), sizeof(buf));
        if (n <= 0) {
            break;
        }
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            in.close();
            return ESP_FAIL;
        }
    }
    in.close();
    return ESP_OK;
}

static esp_err_t get_maintenance(httpd_req_t *req)
{
    HttpTrace trace(req);
    if (!maint_store_ready()) {
        return trace.fail(req, HTTPD_404_NOT_FOUND, "no card");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "[");
    bool first = true;
    MaintListCursor cursor;
    if (maint_store_list_start(&cursor)) {
        MaintItem item;
        while (maint_store_list_next(&cursor, &item)) {
            if (!first) {
                httpd_resp_sendstr_chunk(req, ",");
            }
            char path[56];
            snprintf(path, sizeof(path), "/maint/%s/item.json", item.id);
            if (send_sd_file(req, path) != ESP_OK) {
                maint_store_list_end(&cursor);
                return trace.fail500(req);
            }
            first = false;
        }
        maint_store_list_end(&cursor);
    }
    httpd_resp_sendstr_chunk(req, "]");
    return trace.ok(httpd_resp_sendstr_chunk(req, nullptr));
}

static esp_err_t get_maintenance_item(httpd_req_t *req)
{
    HttpTrace trace(req);
    char id[kMaintIdHex + 1];
    MaintItem item;
    if (!query_id(req, id, sizeof(id)) || !maint_store_load(id, &item)) {
        return trace.fail(req, HTTPD_404_NOT_FOUND, "no item");
    }
    char path[56];
    snprintf(path, sizeof(path), "/maint/%s/item.json", id);
    httpd_resp_set_type(req, "application/json");
    if (send_sd_file(req, path) != ESP_OK) {
        return trace.fail(req, HTTPD_404_NOT_FOUND, "no item");
    }
    return trace.ok(httpd_resp_send_chunk(req, nullptr, 0));
}

static esp_err_t get_maintenance_photo(httpd_req_t *req)
{
    HttpTrace trace(req);
    char id[kMaintIdHex + 1];
    char query[80];
    char nbuf[8];
    if (!query_id(req, id, sizeof(id)) ||
        httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "n", nbuf, sizeof(nbuf)) != ESP_OK) {
        return trace.fail(req, HTTPD_400_BAD_REQUEST, "id and n");
    }
    const unsigned n = static_cast<unsigned>(strtoul(nbuf, nullptr, 10));
    char path[56];
    if (n == 0 || n > kMaintPhotoMax ||
        !maint_store_photo_path(id, static_cast<uint8_t>(n), path, sizeof(path))) {
        return trace.fail(req, HTTPD_404_NOT_FOUND, "no photo");
    }
    File in = SD.open(path, FILE_READ);
    if (!in) {
        return trace.fail(req, HTTPD_404_NOT_FOUND, "no photo");
    }
    httpd_resp_set_type(req, "image/jpeg");
    char buf[1024];
    while (in.available()) {
        const int got = in.read(reinterpret_cast<uint8_t *>(buf), sizeof(buf));
        if (got <= 0) {
            break;
        }
        if (httpd_resp_send_chunk(req, buf, got) != ESP_OK) {
            in.close();
            return trace.fail500(req);
        }
    }
    in.close();
    return trace.ok(httpd_resp_send_chunk(req, nullptr, 0));
}

static esp_err_t post_maintenance(httpd_req_t *req)
{
    HttpTrace trace(req);
    if (!maint_store_ready()) {
        return trace.fail(req, HTTPD_404_NOT_FOUND, "no card");
    }
    /* 200 + 2000 characters can pass 4 KB as UTF-8. Matches the stand-in. */
    if (req->content_len == 0 || req->content_len > kMaintBodyMax) {
        return trace.fail(req, HTTPD_400_BAD_REQUEST, "body size");
    }
    char *body = static_cast<char *>(malloc(req->content_len + 1));
    if (body == nullptr) {
        return trace.fail500(req);
    }
    size_t got = 0;
    while (got < req->content_len) {
        const int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) {
            free(body);
            return trace.fail(req, HTTPD_408_REQ_TIMEOUT, "recv");
        }
        got += static_cast<size_t>(r);
    }
    body[got] = '\0';
    MaintItem item;
    const bool parsed = maint_item_from_json(body, &item);
    free(body);
    if (!parsed || !maint_store_create(&item)) {
        return trace.fail(req, HTTPD_400_BAD_REQUEST, "title, category, priority");
    }
    char resp[96];
    snprintf(resp, sizeof(resp), "{\"id\":\"%s\",\"sync\":\"pending\"}\n", item.id);
    httpd_resp_set_type(req, "application/json");
    return trace.ok(httpd_resp_sendstr(req, resp));
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Query values arrive encoded. '+' is a space. %XX is a byte. */
static void form_decode_inplace(char *s)
{
    char *w = s;
    for (char *r = s; *r != '\0';) {
        if (*r == '+') {
            *w++ = ' ';
            ++r;
        } else if (*r == '%') {
            const int hi = hex_nibble(r[1]);
            const int lo = r[1] != '\0' ? hex_nibble(r[2]) : -1;
            if (hi >= 0 && lo >= 0) {
                *w++ = static_cast<char>((hi << 4) | lo);
                r += 3;
            } else {
                *w++ = *r++;
            }
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

static void trim_note(char *s)
{
    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        ++start;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

static void clip_note(char *s)
{
    if (strlen(s) <= kMaintPhotoNoteMax) {
        return;
    }
    s[kMaintPhotoNoteMax] = '\0';
    size_t i = kMaintPhotoNoteMax;
    while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) {
        s[--i] = '\0';
    }
    if (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0x80) != 0) {
        s[i - 1] = '\0';
    }
}

static esp_err_t post_maintenance_photo(httpd_req_t *req)
{
    HttpTrace trace(req);
    char id[kMaintIdHex + 1];
    char query[768];
    char nbuf[8];
    char note[512];
    note[0] = '\0';
    if (!maint_store_ready() || !query_id(req, id, sizeof(id)) ||
        httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "n", nbuf, sizeof(nbuf)) != ESP_OK) {
        return trace.fail(req, HTTPD_400_BAD_REQUEST, "id and n");
    }
    const unsigned nphoto = static_cast<unsigned>(strtoul(nbuf, nullptr, 10));
    if (nphoto == 0 || nphoto > kMaintPhotoMax || req->content_len == 0 ||
        req->content_len > kMaintPhotoBytesMax) {
        return trace.fail(req, HTTPD_400_BAD_REQUEST, "photo size or index");
    }
    if (!maint_store_photo_begin(id, static_cast<uint8_t>(nphoto))) {
        return trace.fail(req, HTTPD_404_NOT_FOUND, "no item");
    }
    /* One cluster-sized write. 1 KB writes made a 32 KB FAT cluster rewrite itself. */
    static uint8_t buf[16 * 1024];
    size_t got = 0;
    while (got < req->content_len) {
        size_t want = req->content_len - got;
        if (want > sizeof(buf)) {
            want = sizeof(buf);
        }
        const int r = httpd_req_recv(req, reinterpret_cast<char *>(buf), want);
        if (r <= 0) {
            maint_store_photo_abort();
            return trace.fail(req, HTTPD_408_REQ_TIMEOUT, "recv");
        }
        if (!maint_store_photo_write(buf, static_cast<size_t>(r))) {
            maint_store_photo_abort();
            return trace.fail500(req);
        }
        got += static_cast<size_t>(r);
    }
    if (!maint_store_photo_finish()) {
        return trace.fail500(req);
    }
    if (httpd_query_key_value(query, "note", note, sizeof(note)) == ESP_OK) {
        form_decode_inplace(note);
        trim_note(note);
        clip_note(note);
        if (note[0] != '\0') {
            maint_store_set_photo_note(id, static_cast<uint8_t>(nphoto), note);
        }
    }
    httpd_resp_set_type(req, "text/plain");
    return trace.ok(httpd_resp_sendstr(req, "OK\n"));
}

/* Phone calls this only after the club JSON API returns a WordPress id. */
static esp_err_t post_maintenance_ack(httpd_req_t *req)
{
    HttpTrace trace(req);
    char id[kMaintIdHex + 1];
    if (!maint_store_ready() || !query_id(req, id, sizeof(id))) {
        return trace.fail(req, HTTPD_400_BAD_REQUEST, "id");
    }
    if (req->content_len == 0 || req->content_len > 256) {
        return trace.fail(req, HTTPD_400_BAD_REQUEST, "body");
    }
    char body[257];
    size_t got = 0;
    while (got < req->content_len) {
        const int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) {
            return trace.fail(req, HTTPD_408_REQ_TIMEOUT, "recv");
        }
        got += static_cast<size_t>(r);
    }
    body[got] = '\0';
    const uint32_t wp_id = json_get_u32(body, "wp_id");
    if (wp_id == 0) {
        return trace.fail(req, HTTPD_400_BAD_REQUEST, "wp_id");
    }
    MaintItem item;
    if (!maint_store_load(id, &item)) {
        return trace.fail(req, HTTPD_404_NOT_FOUND, "no item");
    }
    item.wp_id = wp_id;
    snprintf(item.sync_state, sizeof(item.sync_state), "synced");
    if (!maint_store_save(&item)) {
        return trace.fail500(req);
    }
    char resp[96];
    snprintf(resp, sizeof(resp),
             "{\"id\":\"%s\",\"wp_id\":%lu,\"sync\":\"synced\"}\n",
             item.id, static_cast<unsigned long>(wp_id));
    httpd_resp_set_type(req, "application/json");
    return trace.ok(httpd_resp_sendstr(req, resp));
}

/* A club edit. The website's modified time wins. Pending boat notes stay. */
static esp_err_t post_maintenance_reconcile(httpd_req_t *req)
{
    HttpTrace trace(req);
    if (!maint_store_ready()) {
        return trace.fail(req, HTTPD_404_NOT_FOUND, "no card");
    }
    if (req->content_len == 0 || req->content_len > kMaintBodyMax) {
        return trace.fail(req, HTTPD_400_BAD_REQUEST, "body size");
    }
    char *body = static_cast<char *>(malloc(req->content_len + 1));
    if (body == nullptr) {
        return trace.fail500(req);
    }
    size_t got = 0;
    while (got < req->content_len) {
        const int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) {
            free(body);
            return trace.fail(req, HTTPD_408_REQ_TIMEOUT, "recv");
        }
        got += static_cast<size_t>(r);
    }
    body[got] = '\0';
    MaintItem incoming;
    const bool parsed = maint_club_item_from_json(body, &incoming);
    free(body);
    if (!parsed) {
        return trace.fail(req, HTTPD_400_BAD_REQUEST, "wp_id, title, category, priority");
    }
    char id[kMaintIdHex + 1];
    const char *action = "kept";
    if (!maint_store_reconcile(&incoming, id, sizeof(id), &action)) {
        return trace.fail500(req);
    }
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"action\":\"%s\",\"id\":\"%s\",\"wp_id\":%lu}\n",
             action, id, static_cast<unsigned long>(incoming.wp_id));
    httpd_resp_set_type(req, "application/json");
    return trace.ok(httpd_resp_sendstr(req, resp));
}

static esp_err_t on_not_found(httpd_req_t *req, httpd_err_code_t err)
{
    HttpTrace trace(req);
    const char *msg = err == HTTPD_405_METHOD_NOT_ALLOWED ? "method" : "not found";
    return trace.fail(req, err, msg);
}

void hub_http_init(void)
{
    g_httpd = nullptr;
}

void hub_http_start(void)
{
    if (g_httpd != nullptr) {
        return;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 16;
    cfg.stack_size = 8192;
    cfg.recv_wait_timeout = 15;
    if (httpd_start(&g_httpd, &cfg) != ESP_OK) {
        Serial.println("hub_http: start failed");
        g_httpd = nullptr;
        return;
    }

    static httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = get_root, .user_ctx = nullptr};
    static httpd_uri_t status = {
        .uri = "/status.json", .method = HTTP_GET, .handler = get_status,
        .user_ctx = nullptr};
    static httpd_uri_t snap = {
        .uri = "/snapshot", .method = HTTP_POST, .handler = post_snapshot,
        .user_ctx = nullptr};
    static httpd_uri_t inj = {
        .uri = "/inject", .method = HTTP_POST, .handler = post_inject,
        .user_ctx = nullptr};
    static httpd_uri_t maint = {
        .uri = "/maintenance", .method = HTTP_GET, .handler = get_maintenance,
        .user_ctx = nullptr};
    static httpd_uri_t maint_post = {
        .uri = "/maintenance", .method = HTTP_POST, .handler = post_maintenance,
        .user_ctx = nullptr};
    static httpd_uri_t maint_item = {
        .uri = "/maintenance/item", .method = HTTP_GET, .handler = get_maintenance_item,
        .user_ctx = nullptr};
    static httpd_uri_t maint_photo = {
        .uri = "/maintenance/photo", .method = HTTP_GET, .handler = get_maintenance_photo,
        .user_ctx = nullptr};
    static httpd_uri_t maint_photo_post = {
        .uri = "/maintenance/photo", .method = HTTP_POST, .handler = post_maintenance_photo,
        .user_ctx = nullptr};
    static httpd_uri_t maint_ack = {
        .uri = "/maintenance/ack", .method = HTTP_POST, .handler = post_maintenance_ack,
        .user_ctx = nullptr};
    static httpd_uri_t maint_reconcile = {
        .uri = "/maintenance/reconcile", .method = HTTP_POST,
        .handler = post_maintenance_reconcile, .user_ctx = nullptr};
    httpd_register_uri_handler(g_httpd, &root);
    httpd_register_uri_handler(g_httpd, &status);
    httpd_register_uri_handler(g_httpd, &snap);
    httpd_register_uri_handler(g_httpd, &inj);
    httpd_register_uri_handler(g_httpd, &maint);
    httpd_register_uri_handler(g_httpd, &maint_post);
    httpd_register_uri_handler(g_httpd, &maint_item);
    httpd_register_uri_handler(g_httpd, &maint_photo);
    httpd_register_uri_handler(g_httpd, &maint_photo_post);
    httpd_register_uri_handler(g_httpd, &maint_ack);
    httpd_register_uri_handler(g_httpd, &maint_reconcile);
    httpd_register_err_handler(g_httpd, HTTPD_404_NOT_FOUND, on_not_found);
    httpd_register_err_handler(g_httpd, HTTPD_405_METHOD_NOT_ALLOWED, on_not_found);
    Serial.println("hub_http: :80  GET /  GET /maintenance  POST /snapshot  POST /inject");
}
