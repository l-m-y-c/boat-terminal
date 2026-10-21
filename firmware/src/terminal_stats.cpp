#include "terminal_stats.h"

#include "gateway.h"
#include "n2k.h"
#include "pairing.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

#include <freertos/FreeRTOS.h>

static char g_json[kTerminalStatsMax] = "{}";
static portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;

static const char *state_code(BusState state)
{
    switch (state) {
    case BusState::Simulated:
        return "sim";
    case BusState::Paused:
        return "paused";
    case BusState::Opening:
        return "opening";
    case BusState::Waiting:
        return "waiting";
    case BusState::Live:
        return "live";
    case BusState::Quiet:
        return "quiet";
    case BusState::BusOff:
        return "bus_off";
    case BusState::Failed:
        return "failed";
    }
    return "unknown";
}

void terminal_stats_refresh(void)
{
    N2kStats n;
    n2k_stats(&n);
    GatewayStats g = {};
    gateway_stats(&g);

    char age[16];
    if (n.last_frame_ms == UINT32_MAX) {
        snprintf(age, sizeof(age), "null");
    } else {
        snprintf(age, sizeof(age), "%lu", static_cast<unsigned long>(n.last_frame_ms));
    }
    const unsigned long lost = static_cast<unsigned long>(n.lost_queue) + n.lost_driver + n.lost_controller;

    char json[kTerminalStatsMax];
    const int len = snprintf(
        json, sizeof(json),
        "{\"v\":1,\"src\":\"%s\",\"state\":\"%s\",\"fps\":%lu,\"fps_peak\":%lu,\"mps\":%lu,"
        "\"frames\":%lu,\"msgs\":%lu,\"q_cap\":%lu,\"q_now\":%lu,\"q_peak\":%lu,"
        "\"lost\":%lu,\"lost_q\":%lu,\"lost_drv\":%lu,\"lost_hw\":%lu,\"ignored\":%lu,\"bus_err\":%lu,\"rec\":%lu,"
        "\"pgns\":%lu,\"devices\":%lu,\"age_ms\":%s,\"up_s\":%lu,"
        "\"apps\":%lu,\"out\":%lu,\"out_lost\":%lu,\"udp_lost\":%lu,\"out_peak\":%lu,\"out_cap\":%lu}",
        n.source == DataSource::Simulator ? "sim" : "n2k", state_code(n.state),
        static_cast<unsigned long>(n.frames_per_s), static_cast<unsigned long>(n.frames_per_s_peak),
        static_cast<unsigned long>(n.messages_per_s), static_cast<unsigned long>(n.frames),
        static_cast<unsigned long>(n.messages), static_cast<unsigned long>(n.queue_capacity),
        static_cast<unsigned long>(n.queue_depth), static_cast<unsigned long>(n.queue_peak), lost,
        static_cast<unsigned long>(n.lost_queue), static_cast<unsigned long>(n.lost_driver),
        static_cast<unsigned long>(n.lost_controller), static_cast<unsigned long>(n.ignored),
        static_cast<unsigned long>(n.bus_errors),
        static_cast<unsigned long>(n.rx_error_counter), static_cast<unsigned long>(n.pgn_types),
        static_cast<unsigned long>(n.devices), age, static_cast<unsigned long>(millis() / 1000UL),
        static_cast<unsigned long>(g.clients), static_cast<unsigned long>(g.sentences),
        static_cast<unsigned long>(g.lost_tcp), static_cast<unsigned long>(g.lost_udp),
        static_cast<unsigned long>(g.queued_peak), static_cast<unsigned long>(g.queue_capacity));
    if (len <= 0 || static_cast<size_t>(len) >= sizeof(json)) {
        return;
    }

    portENTER_CRITICAL(&g_lock);
    memcpy(g_json, json, static_cast<size_t>(len) + 1);
    portEXIT_CRITICAL(&g_lock);
    pairing_set_stats(json);
}

size_t terminal_stats_copy(char *out, size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    portENTER_CRITICAL(&g_lock);
    const size_t len = strnlen(g_json, sizeof(g_json));
    const size_t n = len < cap ? len : cap - 1;
    memcpy(out, g_json, n);
    portEXIT_CRITICAL(&g_lock);
    out[n] = '\0';
    return n;
}
