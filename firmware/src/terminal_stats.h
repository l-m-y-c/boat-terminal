#pragma once

#include <cstddef>

/* The data source, bus and delivery counters as one JSON object. loop()
 * rebuilds it once a second; any task may copy it. The phone reads it on
 * BLE …000050 and GET /status.json carries it as "data". Field names are
 * the contract with mobile/lib/terminal_stats.dart. */
static constexpr size_t kTerminalStatsMax = 512;

void terminal_stats_refresh(void);
size_t terminal_stats_copy(char *out, size_t cap);
