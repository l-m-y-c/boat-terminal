#include "hub_http.h"

#include "inject.h"
#include "nmea.h"
#include "pairing.h"
#include "snapshot.h"
#include "gateway.h"

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
    "</body></html>";

static esp_err_t get_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t get_status(httpd_req_t *req)
{
    char json[256];
    snprintf(json, sizeof(json),
             "{\"boat\":\"%s\",\"tcp\":%lu,\"nmea_good\":%lu,\"nmea_bad\":%lu,"
             "\"inject_local\":%s,\"snapshot\":%s}\n",
             LMYC_BOAT_ID,
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
    cfg.max_uri_handlers = 8;
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
    httpd_register_uri_handler(g_httpd, &root);
    httpd_register_uri_handler(g_httpd, &status);
    httpd_register_uri_handler(g_httpd, &snap);
    httpd_register_uri_handler(g_httpd, &inj);
    Serial.println("hub_http: :80  GET /  POST /snapshot  POST /inject");
}
