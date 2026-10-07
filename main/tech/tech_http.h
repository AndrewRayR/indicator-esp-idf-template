#ifndef TECH_HTTP_H
#define TECH_HTTP_H

#include <stddef.h>
#include "esp_err.h"

#define TECH_HTTP_MAX_BODY 8192

typedef struct {
    int status_code;
    size_t body_length;
    char body[TECH_HTTP_MAX_BODY + 1];
} tech_http_response_t;

/* Synchronous bounded GET; call from a worker task, never the LVGL task. */
esp_err_t tech_http_get(const char *url, const char *accept,
                        tech_http_response_t *response);

#endif