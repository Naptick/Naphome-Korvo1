#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*sensor_manager_sample_cb_t)(cJSON *sensor_root);

typedef struct {
    const char *name;
    sensor_manager_sample_cb_t sample_cb;
} sensor_manager_sensor_t;

typedef void (*sensor_manager_observer_cb_t)(const char *sensor_name,
                                             const cJSON *sensor_state,
                                             void *user_ctx);

typedef struct {
    uint32_t publish_interval_ms;
} sensor_manager_config_t;

esp_err_t sensor_manager_init(const sensor_manager_config_t *config);
esp_err_t sensor_manager_register(const sensor_manager_sensor_t *sensor);
esp_err_t sensor_manager_set_observer(sensor_manager_observer_cb_t observer, void *user_ctx);
esp_err_t sensor_manager_start(void);
esp_err_t sensor_manager_stop(void);
bool sensor_manager_is_running(void);

#ifdef __cplusplus
}
#endif

