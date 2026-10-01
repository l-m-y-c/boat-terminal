#include "hub_http.h"

#include "inject.h"
#include "maint_store.h"
#include "nmea.h"
#include "pairing.h"
#include "snapshot.h"
#include "gateway.h"

#include <SD.h>

#include <Arduino.h>
#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include <cstring>

#ifndef AP_PASSWORD
#define AP_PASSWORD "lmycboat"
#endif

static httpd_handle_t g_httpd = nullptr;
static char g_last_err[48] = "";
static uint32_t g_last_snap_ms = 0;

const char *hub_http_last_error(void) { return g_last_err; }

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
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t get_status(httpd_req_t *req)
{
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
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t post_snapshot(httpd_req_t *req)
{
    const size_t n = req->content_len;
    if (n == 0 || n > kSnapshotJpegMax) {
        snprintf(g_last_err, sizeof(g_last_err), "bad jpeg size %u",
                 static_cast<unsigned>(n));
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "jpeg too large");
        return ESP_FAIL;
    }
    const uint32_t now = millis();
    if (g_last_snap_ms != 0 && (now - g_last_snap_ms) < 1000) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "rate 1 fps");
        return ESP_FAIL;
    }

    uint8_t *buf = static_cast<uint8_t *>(
        heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buf == nullptr) {
        buf = static_cast<uint8_t *>(malloc(n));
    }
    if (buf == nullptr) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    size_t got = 0;
    while (got < n) {
        const int r = httpd_req_recv(req, reinterpret_cast<char *>(buf + got), n - got);
        if (r <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "recv");
            return ESP_FAIL;
        }
        got += static_cast<size_t>(r);
    }

    const bool ok = snapshot_load_jpeg(buf, n);
    free(buf);
    if (!ok) {
        snprintf(g_last_err, sizeof(g_last_err), "decode failed");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "decode failed");
        return ESP_FAIL;
    }
    g_last_snap_ms = now;
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "OK\n");
}

static esp_err_t post_inject(httpd_req_t *req)
{
    if (!inject_local_armed()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "LOCAL not armed on 7 inch");
        return ESP_FAIL;
    }
    char body[512];
    const int n = httpd_req_recv(req, body, sizeof(body) - 1);
    if (n <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
        return ESP_FAIL;
    }
    body[n] = '\0';
    char *p = body;
    int accepted = 0;
    while (*p != '\0') {
        char *eol = strpbrk(p, "\r\n");
        if (eol != nullptr) {
            *eol = '\0';
        }
        if (p[0] != '\0' && inject_is_gnss_sentence(p)) {
            nmea_feed_line(p);
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
    return httpd_resp_sendstr(req, resp);
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
    if (!maint_store_ready()) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no card");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "[");
    bool first = true;
    if (maint_store_list_start()) {
        MaintItem item;
        while (maint_store_list_next(&item)) {
            if (!first) {
                httpd_resp_sendstr_chunk(req, ",");
            }
            char path[56];
            snprintf(path, sizeof(path), "/maint/%s/item.json", item.id);
            if (send_sd_file(req, path) != ESP_OK) {
                maint_store_list_end();
                return ESP_FAIL;
            }
            first = false;
        }
        maint_store_list_end();
    }
    httpd_resp_sendstr_chunk(req, "]");
    return httpd_resp_sendstr_chunk(req, nullptr);
}

static esp_err_t get_maintenance_item(httpd_req_t *req)
{
    char id[kMaintIdHex + 1];
    MaintItem item;
    if (!query_id(req, id, sizeof(id)) || !maint_store_load(id, &item)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no item");
        return ESP_FAIL;
    }
    char path[56];
    snprintf(path, sizeof(path), "/maint/%s/item.json", id);
    httpd_resp_set_type(req, "application/json");
    if (send_sd_file(req, path) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no item");
        return ESP_FAIL;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t get_maintenance_photo(httpd_req_t *req)
{
    char id[kMaintIdHex + 1];
    char query[80];
    char nbuf[8];
    if (!query_id(req, id, sizeof(id)) ||
        httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "n", nbuf, sizeof(nbuf)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id and n");
        return ESP_FAIL;
    }
    const unsigned n = static_cast<unsigned>(strtoul(nbuf, nullptr, 10));
    char path[56];
    if (n == 0 || n > kMaintPhotoMax ||
        !maint_store_photo_path(id, static_cast<uint8_t>(n), path, sizeof(path))) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no photo");
        return ESP_FAIL;
    }
    File in = SD.open(path, FILE_READ);
    if (!in) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no photo");
        return ESP_FAIL;
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
            return ESP_FAIL;
        }
    }
    in.close();
    return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t post_maintenance(httpd_req_t *req)
{
    if (!maint_store_ready()) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no card");
        return ESP_FAIL;
    }
    if (req->content_len == 0 || req->content_len > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body size");
        return ESP_FAIL;
    }
    char *body = static_cast<char *>(malloc(req->content_len + 1));
    if (body == nullptr) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        const int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) {
            free(body);
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "recv");
            return ESP_FAIL;
        }
        got += static_cast<size_t>(r);
    }
    body[got] = '\0';
    MaintItem item;
    const bool parsed = maint_item_from_json(body, &item);
    free(body);
    if (!parsed || !maint_store_create(&item)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "title, category, priority");
        return ESP_FAIL;
    }
    char resp[96];
    snprintf(resp, sizeof(resp), "{\"id\":\"%s\",\"sync\":\"pending\"}\n", item.id);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, resp);
}

static esp_err_t post_maintenance_photo(httpd_req_t *req)
{
    char id[kMaintIdHex + 1];
    char query[80];
    char nbuf[8];
    if (!maint_store_ready() || !query_id(req, id, sizeof(id)) ||
        httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "n", nbuf, sizeof(nbuf)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id and n");
        return ESP_FAIL;
    }
    const unsigned nphoto = static_cast<unsigned>(strtoul(nbuf, nullptr, 10));
    if (nphoto == 0 || nphoto > kMaintPhotoMax || req->content_len == 0 ||
        req->content_len > kMaintPhotoBytesMax) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "photo size or index");
        return ESP_FAIL;
    }
    if (!maint_store_photo_begin(id, static_cast<uint8_t>(nphoto))) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no item");
        return ESP_FAIL;
    }
    uint8_t buf[1024];
    size_t got = 0;
    while (got < req->content_len) {
        size_t want = req->content_len - got;
        if (want > sizeof(buf)) {
            want = sizeof(buf);
        }
        const int r = httpd_req_recv(req, reinterpret_cast<char *>(buf), want);
        if (r <= 0) {
            maint_store_photo_abort();
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "recv");
            return ESP_FAIL;
        }
        if (!maint_store_photo_write(buf, static_cast<size_t>(r))) {
            maint_store_photo_abort();
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        got += static_cast<size_t>(r);
    }
    if (!maint_store_photo_finish()) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "OK\n");
}

/* Phone calls this only after the club JSON API returns a WordPress id. */
static esp_err_t post_maintenance_ack(httpd_req_t *req)
{
    char id[kMaintIdHex + 1];
    if (!maint_store_ready() || !query_id(req, id, sizeof(id))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id");
        return ESP_FAIL;
    }
    if (req->content_len == 0 || req->content_len > 256) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body");
        return ESP_FAIL;
    }
    char body[257];
    size_t got = 0;
    while (got < req->content_len) {
        const int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "recv");
            return ESP_FAIL;
        }
        got += static_cast<size_t>(r);
    }
    body[got] = '\0';
    const char *key = strstr(body, "\"wp_id\"");
    uint32_t wp_id = 0;
    if (key != nullptr) {
        key += 7;
        while (*key == ' ' || *key == '\t' || *key == ':') {
            ++key;
        }
        wp_id = static_cast<uint32_t>(strtoul(key, nullptr, 10));
    }
    if (wp_id == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "wp_id");
        return ESP_FAIL;
    }
    MaintItem item;
    if (!maint_store_load(id, &item)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no item");
        return ESP_FAIL;
    }
    item.wp_id = wp_id;
    snprintf(item.sync_state, sizeof(item.sync_state), "synced");
    if (!maint_store_save(&item)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    char resp[96];
    snprintf(resp, sizeof(resp),
             "{\"id\":\"%s\",\"wp_id\":%lu,\"sync\":\"synced\"}\n",
             item.id, static_cast<unsigned long>(wp_id));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, resp);
}

/* A club edit. The website's modified time wins. Pending boat notes stay. */
static esp_err_t post_maintenance_reconcile(httpd_req_t *req)
{
    if (!maint_store_ready()) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no card");
        return ESP_FAIL;
    }
    if (req->content_len == 0 || req->content_len > 8192) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body size");
        return ESP_FAIL;
    }
    char *body = static_cast<char *>(malloc(req->content_len + 1));
    if (body == nullptr) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        const int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) {
            free(body);
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "recv");
            return ESP_FAIL;
        }
        got += static_cast<size_t>(r);
    }
    body[got] = '\0';
    MaintItem incoming;
    const bool parsed = maint_club_item_from_json(body, &incoming);
    free(body);
    if (!parsed) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "wp_id, title, category, priority");
        return ESP_FAIL;
    }
    char id[kMaintIdHex + 1];
    const char *action = "kept";
    if (!maint_store_reconcile(&incoming, id, sizeof(id), &action)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"action\":\"%s\",\"id\":\"%s\",\"wp_id\":%lu}\n",
             action, id, static_cast<unsigned long>(incoming.wp_id));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, resp);
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
    Serial.println("hub_http: :80  GET /  GET /maintenance  POST /snapshot  POST /inject");
}
