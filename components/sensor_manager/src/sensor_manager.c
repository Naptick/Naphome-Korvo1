/**
 * @file sensor_manager.c
 */

#include "sensor_manager.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "somnus_profile.h"
#include "cJSON.h"
#include <time.h>
#include <string.h>

#define SENSOR_MANAGER_TAG "sensor_manager"

#ifndef CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS
#define CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS 10000
#endif

#define SENSOR_MANAGER_MAX_SENSORS 8
#define SENSOR_MANAGER_TASK_STACK 4096
#define SENSOR_MANAGER_TASK_PRIO 5

typedef struct {
    const char *name;
    sensor_manager_sample_cb_t callback;
} sensor_entry_t;

static sensor_entry_t s_sensors[SENSOR_MANAGER_MAX_SENSORS];
static size_t s_sensor_count;
static uint32_t s_publish_interval_ms = CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS;
static bool s_initialized;
static bool s_should_run;
static bool s_running;
static TaskHandle_t s_task_handle;
static sensor_manager_observer_cb_t s_observer_cb;
static void *s_observer_ctx;

static void sensor_manager_collect_and_publish(void);
static void sensor_manager_task(void *arg);
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    // Basic HTTP event handler - can be extended for more detailed logging
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(SENSOR_MANAGER_TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(SENSOR_MANAGER_TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(SENSOR_MANAGER_TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(SENSOR_MANAGER_TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(SENSOR_MANAGER_TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(SENSOR_MANAGER_TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(SENSOR_MANAGER_TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t sensor_manager_init(const sensor_manager_config_t *config)
{
    if (config && config->publish_interval_ms > 0) {
        s_publish_interval_ms = config->publish_interval_ms;
    } else {
        s_publish_interval_ms = CONFIG_SENSOR_MANAGER_PUBLISH_INTERVAL_MS;
    }

    s_initialized = true;
    s_observer_cb = NULL;
    s_observer_ctx = NULL;
    return ESP_OK;
}

esp_err_t sensor_manager_register(const sensor_manager_sensor_t *sensor)
{
    if (!s_initialized) {
        sensor_manager_init(NULL);
    }

    ESP_RETURN_ON_FALSE(sensor, ESP_ERR_INVALID_ARG, SENSOR_MANAGER_TAG, "sensor pointer is NULL");
    ESP_RETURN_ON_FALSE(sensor->name && sensor->name[0] != '\0',
                        ESP_ERR_INVALID_ARG,
                        SENSOR_MANAGER_TAG,
                        "sensor name is invalid");
    ESP_RETURN_ON_FALSE(sensor->sample_cb,
                        ESP_ERR_INVALID_ARG,
                        SENSOR_MANAGER_TAG,
                        "sensor callback is NULL");
    ESP_RETURN_ON_FALSE(!s_running,
                        ESP_ERR_INVALID_STATE,
                        SENSOR_MANAGER_TAG,
                        "cannot register sensors while manager is running");
    ESP_RETURN_ON_FALSE(s_sensor_count < SENSOR_MANAGER_MAX_SENSORS,
                        ESP_ERR_NO_MEM,
                        SENSOR_MANAGER_TAG,
                        "sensor registry full");

    s_sensors[s_sensor_count++] = (sensor_entry_t){
        .name = sensor->name,
        .callback = sensor->sample_cb,
    };
    return ESP_OK;
}

esp_err_t sensor_manager_set_observer(sensor_manager_observer_cb_t observer, void *user_ctx)
{
    s_observer_cb = observer;
    s_observer_ctx = user_ctx;
    return ESP_OK;
}

esp_err_t sensor_manager_start(void)
{
    if (!s_initialized) {
        sensor_manager_init(NULL);
    }

    ESP_RETURN_ON_FALSE(s_sensor_count > 0,
                        ESP_ERR_INVALID_STATE,
                        SENSOR_MANAGER_TAG,
                        "no sensors registered");

    if (s_running) {
        return ESP_OK;
    }

    s_should_run = true;
    BaseType_t created = xTaskCreate(sensor_manager_task,
                                     "sensor_manager",
                                     SENSOR_MANAGER_TASK_STACK,
                                     NULL,
                                     SENSOR_MANAGER_TASK_PRIO,
                                     &s_task_handle);
    ESP_RETURN_ON_FALSE(created == pdPASS,
                        ESP_ERR_NO_MEM,
                        SENSOR_MANAGER_TAG,
                        "failed to create sensor manager task");

    return ESP_OK;
}

esp_err_t sensor_manager_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    s_should_run = false;
    while (s_running) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

bool sensor_manager_is_running(void)
{
    return s_running;
}

static void sensor_manager_task(void *arg)
{
    (void)arg;
    const TickType_t delay_ticks = pdMS_TO_TICKS(s_publish_interval_ms);
    TickType_t last_wake = xTaskGetTickCount();
    s_running = true;

    while (s_should_run) {
        sensor_manager_collect_and_publish();
        vTaskDelayUntil(&last_wake, delay_ticks);
    }

    s_running = false;
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static void sensor_manager_collect_and_publish(void)
{
    if (s_sensor_count == 0) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(SENSOR_MANAGER_TAG, "Failed to allocate JSON root");
        return;
    }

    // Get device ID
    char device_id[32] = {0};
    esp_err_t device_id_err = somnus_profile_get_device_id(device_id, sizeof(device_id));
    if (device_id_err == ESP_OK) {
        cJSON_AddStringToObject(root, "deviceId", device_id);
    } else {
        cJSON_AddStringToObject(root, "deviceId", "UNKNOWN");
    }

    // Format ISO 8601 timestamp
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char timestamp_str[32];
    strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    cJSON_AddStringToObject(root, "timestamp", timestamp_str);

    // Collect sensor data
    cJSON *sensors_obj = cJSON_CreateObject();
    bool has_data = false;

    for (size_t i = 0; i < s_sensor_count; ++i) {
        const sensor_entry_t *entry = &s_sensors[i];
        cJSON *sensor_obj = cJSON_CreateObject();
        if (!sensor_obj) {
            ESP_LOGW(SENSOR_MANAGER_TAG, "Failed to allocate JSON object for sensor '%s'", entry->name);
            continue;
        }

        bool ok = entry->callback(sensor_obj);
        if (ok && sensor_obj->child) {
            // Transform sensor data to API format
            // API expects: { "temperature": 23.2, "humidity": 48, "co2": 450, "pm2_5": 15, etc. }
            
            if (strcmp(entry->name, "sht4x") == 0) {
                // Sensirion SHT4x (Temperature & Humidity)
                cJSON *temp = cJSON_GetObjectItem(sensor_obj, "temperature_c");
                cJSON *hum = cJSON_GetObjectItem(sensor_obj, "humidity_rh");
                if (temp && cJSON_IsNumber(temp)) {
                    cJSON_AddNumberToObject(sensors_obj, "temperature", temp->valuedouble);
                    has_data = true;
                }
                if (hum && cJSON_IsNumber(hum)) {
                    cJSON_AddNumberToObject(sensors_obj, "humidity", hum->valuedouble);
                    has_data = true;
                }
            } else if (strcmp(entry->name, "cm1106s") == 0) {
                // Cubic CM1106S (CO2)
                cJSON *co2 = cJSON_GetObjectItem(sensor_obj, "co2_ppm");
                if (co2 && cJSON_IsNumber(co2)) {
                    cJSON_AddNumberToObject(sensors_obj, "co2", co2->valuedouble);
                    has_data = true;
                }
            } else if (strcmp(entry->name, "pm2012") == 0) {
                // Cubic PM2012 (PM1.0+2.5+10+VOC)
                cJSON *pm1 = cJSON_GetObjectItem(sensor_obj, "pm1_0_ug_m3");
                cJSON *pm25 = cJSON_GetObjectItem(sensor_obj, "pm2_5_ug_m3");
                cJSON *pm10 = cJSON_GetObjectItem(sensor_obj, "pm10_ug_m3");
                cJSON *voc = cJSON_GetObjectItem(sensor_obj, "voc_index");
                if (pm1 && cJSON_IsNumber(pm1)) {
                    cJSON_AddNumberToObject(sensors_obj, "pm1_0", pm1->valuedouble);
                    has_data = true;
                }
                if (pm25 && cJSON_IsNumber(pm25)) {
                    cJSON_AddNumberToObject(sensors_obj, "pm2_5", pm25->valuedouble);
                    has_data = true;
                }
                if (pm10 && cJSON_IsNumber(pm10)) {
                    cJSON_AddNumberToObject(sensors_obj, "pm10", pm10->valuedouble);
                    has_data = true;
                }
                if (voc && cJSON_IsNumber(voc)) {
                    cJSON_AddNumberToObject(sensors_obj, "voc", voc->valueint);
                    has_data = true;
                }
            } else if (strcmp(entry->name, "tsl2561") == 0) {
                // AMS TSL2561 (Light)
                cJSON *lux = cJSON_GetObjectItem(sensor_obj, "light_lux");
                if (lux && cJSON_IsNumber(lux)) {
                    cJSON_AddNumberToObject(sensors_obj, "light", lux->valueint);
                    has_data = true;
                }
            } else if (strcmp(entry->name, "as7341") == 0) {
                // AS7341 (UV Light)
                cJSON *uv = cJSON_GetObjectItem(sensor_obj, "uv_index");
                if (uv && cJSON_IsNumber(uv)) {
                    cJSON_AddNumberToObject(sensors_obj, "uv_index", uv->valueint);
                    has_data = true;
                }
            } else if (strcmp(entry->name, "veml7700") == 0) {
                // VISHAY VEML7700 (Ambient Light)
                cJSON *ambient = cJSON_GetObjectItem(sensor_obj, "ambient_lux");
                if (ambient && cJSON_IsNumber(ambient)) {
                    cJSON_AddNumberToObject(sensors_obj, "ambient_lux", ambient->valueint);
                    has_data = true;
                }
            }
            
            if (s_observer_cb) {
                s_observer_cb(entry->name, sensor_obj, s_observer_ctx);
            }
            cJSON_Delete(sensor_obj);
        } else {
            cJSON_Delete(sensor_obj);
        }
    }

    if (!has_data) {
        cJSON_Delete(sensors_obj);
        cJSON_Delete(root);
        return;
    }

    cJSON_AddItemToObject(root, "sensors", sensors_obj);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!payload) {
        ESP_LOGW(SENSOR_MANAGER_TAG, "Failed to serialise telemetry payload");
        return;
    }

    // Publish to HTTP API
    esp_http_client_config_t config = {
        .url = "https://api-uat.naptick.com/sensor-service/sensor-service/stream",
        .event_handler = http_event_handler,
        .timeout_ms = 5000,
        .skip_cert_common_name_check = false,  // Verify certificate CN
        .crt_bundle_attach = esp_crt_bundle_attach,  // Use certificate bundle for verification
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, payload, strlen(payload));
        
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            ESP_LOGI(SENSOR_MANAGER_TAG, "Sensor data published: HTTP %d", status_code);
        } else {
            ESP_LOGW(SENSOR_MANAGER_TAG, "HTTP publish failed: %s", esp_err_to_name(err));
        }
        esp_http_client_cleanup(client);
    } else {
        ESP_LOGE(SENSOR_MANAGER_TAG, "Failed to initialize HTTP client");
    }

    free(payload);
}

