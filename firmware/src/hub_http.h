#pragma once

#include <cstddef>

void hub_http_init(void);
void hub_http_start(void);
const char *hub_http_last_error(void);
bool hub_http_listening(void);
/* Newest request, including the status once the handler finishes. */
void hub_http_copy_last(char *out, size_t n);
/* Newest requests, one per line, oldest first. */
void hub_http_copy_log(char *out, size_t n);
