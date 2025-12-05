#include "tls_mutex.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "tls_mutex";
static SemaphoreHandle_t s_tls_mutex = NULL;

esp_err_t tls_mutex_init(void)
{
    if (s_tls_mutex != NULL) {
        ESP_LOGW(TAG, "TLS mutex already initialized");
        return ESP_OK;
    }

    // Create a binary semaphore (mutex) for TLS connections
    // Only one TLS connection can be active at a time
    s_tls_mutex = xSemaphoreCreateBinary();
    if (s_tls_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create TLS mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initially give the semaphore (available)
    xSemaphoreGive(s_tls_mutex);

    ESP_LOGI(TAG, "TLS mutex initialized");
    return ESP_OK;
}

esp_err_t tls_mutex_take(TickType_t timeout_ms)
{
    if (s_tls_mutex == NULL) {
        ESP_LOGE(TAG, "TLS mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t timeout_ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    
    if (xSemaphoreTake(s_tls_mutex, timeout_ticks) == pdTRUE) {
        ESP_LOGD(TAG, "TLS mutex acquired");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Failed to acquire TLS mutex (timeout: %lu ms)", (unsigned long)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t tls_mutex_give(void)
{
    if (s_tls_mutex == NULL) {
        ESP_LOGE(TAG, "TLS mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreGive(s_tls_mutex) == pdTRUE) {
        ESP_LOGD(TAG, "TLS mutex released");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to release TLS mutex (not taken?)");
        return ESP_FAIL;
    }
}

esp_err_t tls_mutex_deinit(void)
{
    if (s_tls_mutex != NULL) {
        vSemaphoreDelete(s_tls_mutex);
        s_tls_mutex = NULL;
        ESP_LOGI(TAG, "TLS mutex deinitialized");
    }
    return ESP_OK;
}
