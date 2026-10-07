#include "tech_http.h"
#include <stdbool.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"

#define TECH_HTTP_TIMEOUT_MS 10000

typedef struct {
    tech_http_response_t *response;
    bool overflow;
} response_context_t;

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    response_context_t *context = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }
    size_t remaining = TECH_HTTP_MAX_BODY - context->response->body_length;
    if ((size_t)event->data_len > remaining) {
        context->overflow = true;
        return ESP_FAIL;
    }
    memcpy(context->response->body + context->response->body_length,
           event->data, event->data_len);
    context->response->body_length += event->data_len;
    context->response->body[context->response->body_length] = '\0';
    return ESP_OK;
}
esp_err_t tech_http_get(const char *url, const char *accept,
                        tech_http_response_t *response)
{
    if (url == NULL || response == NULL ||
        (strncmp(url, "https://", 8) != 0 && strncmp(url, "http://", 7) != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(response, 0, sizeof(*response));
    response_context_t context = {.response = response};

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &context,
        .timeout_ms = TECH_HTTP_TIMEOUT_MS,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = false,
        .max_redirection_count = 3,
        .keep_alive_enable = false
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_ERR_NO_MEM;

    if (accept != NULL) {
        esp_http_client_set_header(client, "Accept", accept);
    }
    esp_http_client_set_header(client, "User-Agent", "D1-Tech-Terminal/1.0");
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        response->status_code = esp_http_client_get_status_code(client);
    } else if (context.overflow) {
        err = ESP_ERR_NO_MEM;
    }
    esp_http_client_cleanup(client);
    return err;
}
