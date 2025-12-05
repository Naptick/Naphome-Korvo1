#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct webserver webserver_t;

typedef struct {
    int port;  // HTTP server port (default: 80)
} webserver_config_t;

/**
 * Initialize and start the HTTP webserver
 * @param out_server Output pointer for server handle
 * @param cfg Configuration structure
 * @return ESP_OK on success
 */
esp_err_t webserver_start(webserver_t **out_server, const webserver_config_t *cfg);

/**
 * Stop and deinitialize the webserver
 * @param server Server handle
 */
void webserver_stop(webserver_t *server);

/**
 * Check if webserver is running
 * @param server Server handle
 * @return true if running
 */
bool webserver_is_running(webserver_t *server);

#ifdef __cplusplus
}
#endif
