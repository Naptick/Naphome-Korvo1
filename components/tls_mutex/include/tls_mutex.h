#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize TLS mutex (semaphore) for serializing TLS connections
 * @return ESP_OK on success
 */
esp_err_t tls_mutex_init(void);

/**
 * @brief Acquire TLS mutex (blocking)
 * Call this before making any TLS/HTTPS connection
 * @param timeout_ms Maximum time to wait in milliseconds (portMAX_DELAY for infinite)
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if timeout
 */
esp_err_t tls_mutex_take(TickType_t timeout_ms);

/**
 * @brief Release TLS mutex
 * Call this after TLS/HTTPS connection is complete
 * @return ESP_OK on success
 */
esp_err_t tls_mutex_give(void);

/**
 * @brief Deinitialize TLS mutex
 * @return ESP_OK on success
 */
esp_err_t tls_mutex_deinit(void);

#ifdef __cplusplus
}
#endif
