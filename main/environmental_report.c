#include "environmental_report.h"
#include "sensor_integration.h"
#include "gemini_api.h"
#include "voice_assistant.h"
#include "wake_word_manager.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "time.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "env_report";

// Open-Meteo API configuration (free, no API key required)
#define OPEN_METEO_LAT "39.09"   // Default: Colorado Springs area (can be configured)
#define OPEN_METEO_LON "-104.87"
#define OPEN_METEO_BASE_URL "https://api.open-meteo.com/v1/forecast"

// Structure to hold response data
typedef struct {
    char *buffer;
    size_t buffer_size;
    size_t written;
} http_response_data_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_data_t *data = (http_response_data_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client) && data && data->buffer) {
                size_t available = data->buffer_size - data->written - 1;
                size_t to_copy = (evt->data_len < available) ? evt->data_len : available;
                if (to_copy > 0) {
                    memcpy(data->buffer + data->written, evt->data, to_copy);
                    data->written += to_copy;
                    data->buffer[data->written] = '\0';
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t environmental_report_fetch_weather_data(char *weather_json, size_t weather_json_len,
                                                   char *air_quality_json, size_t air_quality_json_len)
{
    if (!weather_json && !air_quality_json) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;

    // Fetch combined weather and air quality data from Open-Meteo
    // Open-Meteo provides both weather and air quality in one request
    if (weather_json && weather_json_len > 0) {
        char url[512];
        snprintf(url, sizeof(url), "%s?latitude=%s&longitude=%s&hourly=temperature_2m,relative_humidity_2m,pm2_5,pm10,ozone&current=temperature_2m,relative_humidity_2m,pm2_5,pm10,ozone",
                 OPEN_METEO_BASE_URL, OPEN_METEO_LAT, OPEN_METEO_LON);

        http_response_data_t response_data = {
            .buffer = weather_json,
            .buffer_size = weather_json_len,
            .written = 0
        };

        esp_http_client_config_t config = {
            .url = url,
            .event_handler = http_event_handler,
            .timeout_ms = 10000,
            .skip_cert_common_name_check = false,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .user_data = &response_data,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            ESP_LOGE(TAG, "Failed to initialize HTTP client for weather/air quality");
            ret = ESP_ERR_NO_MEM;
        } else {
            esp_err_t http_ret = esp_http_client_perform(client);
            if (http_ret == ESP_OK) {
                int status_code = esp_http_client_get_status_code(client);
                if (status_code == 200) {
                    ESP_LOGI(TAG, "Weather/air quality data fetched successfully (%zu bytes)", response_data.written);
                    // Copy to air_quality_json if provided (same data)
                    if (air_quality_json && air_quality_json_len > 0) {
                        size_t copy_len = (response_data.written < air_quality_json_len - 1) 
                                        ? response_data.written : air_quality_json_len - 1;
                        memcpy(air_quality_json, weather_json, copy_len);
                        air_quality_json[copy_len] = '\0';
                    }
                } else {
                    ESP_LOGW(TAG, "Weather API returned status %d", status_code);
                    weather_json[0] = '\0';
                    if (air_quality_json) air_quality_json[0] = '\0';
                }
            } else {
                ESP_LOGW(TAG, "Failed to fetch weather/air quality: %s", esp_err_to_name(http_ret));
                weather_json[0] = '\0';
                if (air_quality_json) air_quality_json[0] = '\0';
            }
            esp_http_client_cleanup(client);
        }
    }

    return ret;
}

static void format_sensor_data_string(char *buffer, size_t buffer_len, const sensor_integration_data_t *data)
{
    snprintf(buffer, buffer_len,
             "Indoor Environment:\n"
             "- Temperature: %.1f°C (%.1f°F)\n"
             "- Humidity: %.1f%%\n"
             "- CO2: %.0f ppm\n"
             "- PM2.5: %.1f μg/m³\n"
             "- PM10: %.1f μg/m³\n"
             "- VOC Index: %d\n"
             "- Light: %d lux\n"
             "- UV Index: %d\n"
             "- Ambient Light: %d lux\n",
             data->temperature_c, data->temperature_c * 9.0f / 5.0f + 32.0f,
             data->humidity_rh,
             data->co2_ppm,
             data->pm2_5_ug_m3,
             data->pm10_ug_m3,
             data->voc_index,
             data->light_lux,
             data->uv_index,
             data->ambient_lux);
}

static void format_weather_summary(char *buffer, size_t buffer_len, const char *weather_json)
{
    if (!weather_json || strlen(weather_json) == 0) {
        snprintf(buffer, buffer_len, "Weather data unavailable");
        return;
    }

    cJSON *json = cJSON_Parse(weather_json);
    if (!json) {
        snprintf(buffer, buffer_len, "Weather data unavailable (parse error)");
        return;
    }

    // Open-Meteo format: current.temperature_2m, current.relative_humidity_2m
    cJSON *current = cJSON_GetObjectItem(json, "current");
    if (current) {
        cJSON *temp_item = cJSON_GetObjectItem(current, "temperature_2m");
        cJSON *humidity_item = cJSON_GetObjectItem(current, "relative_humidity_2m");
        
        if (temp_item && humidity_item) {
            double temp = temp_item->valuedouble;
            double humidity = humidity_item->valuedouble;

            snprintf(buffer, buffer_len,
                     "Outdoor Weather:\n"
                     "- Temperature: %.1f°C (%.1f°F)\n"
                     "- Humidity: %.0f%%",
                     temp, temp * 9.0 / 5.0 + 32.0, humidity);
        } else {
            snprintf(buffer, buffer_len, "Weather data incomplete");
        }
    } else {
        snprintf(buffer, buffer_len, "Weather data unavailable");
    }

    cJSON_Delete(json);
}

static void format_air_quality_summary(char *buffer, size_t buffer_len, const char *air_quality_json)
{
    if (!air_quality_json || strlen(air_quality_json) == 0) {
        snprintf(buffer, buffer_len, "Air quality data unavailable");
        return;
    }

    cJSON *json = cJSON_Parse(air_quality_json);
    if (!json) {
        snprintf(buffer, buffer_len, "Air quality data unavailable (parse error)");
        return;
    }

    // Open-Meteo format: current.pm2_5, current.pm10, current.ozone
    cJSON *current = cJSON_GetObjectItem(json, "current");
    if (current) {
        cJSON *pm2_5_item = cJSON_GetObjectItem(current, "pm2_5");
        cJSON *pm10_item = cJSON_GetObjectItem(current, "pm10");
        cJSON *ozone_item = cJSON_GetObjectItem(current, "ozone");

        if (pm2_5_item && pm10_item) {
            double pm2_5 = pm2_5_item->valuedouble;
            double pm10 = pm10_item->valuedouble;
            double ozone = ozone_item ? ozone_item->valuedouble : 0.0;

            // Calculate AQI from PM2.5 (simplified US AQI)
            int aqi = 0;
            const char *aqi_str = "Unknown";
            if (pm2_5 <= 12.0) {
                aqi = 1 + (int)((pm2_5 / 12.0) * 50);
                aqi_str = "Good";
            } else if (pm2_5 <= 35.4) {
                aqi = 51 + (int)(((pm2_5 - 12.0) / 23.4) * 49);
                aqi_str = "Moderate";
            } else if (pm2_5 <= 55.4) {
                aqi = 101 + (int)(((pm2_5 - 35.4) / 20.0) * 49);
                aqi_str = "Unhealthy for Sensitive Groups";
            } else if (pm2_5 <= 150.4) {
                aqi = 151 + (int)(((pm2_5 - 55.4) / 95.0) * 49);
                aqi_str = "Unhealthy";
            } else {
                aqi = 201;
                aqi_str = "Very Unhealthy";
            }

            snprintf(buffer, buffer_len,
                     "Outdoor Air Quality:\n"
                     "- AQI: %d (%s)\n"
                     "- PM2.5: %.1f μg/m³\n"
                     "- PM10: %.1f μg/m³\n"
                     "- Ozone: %.1f μg/m³",
                     aqi, aqi_str, pm2_5, pm10, ozone);
        } else {
            snprintf(buffer, buffer_len, "Air quality data incomplete");
        }
    } else {
        snprintf(buffer, buffer_len, "Air quality data unavailable");
    }

    cJSON_Delete(json);
}

esp_err_t environmental_report_generate_and_speak(void)
{
    ESP_LOGI(TAG, "Generating environmental report...");

    // Get current time
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%A, %B %d, %Y at %I:%M %p", &timeinfo);

    // Get sensor data
    sensor_integration_data_t sensor_data = sensor_integration_get_data();
    char sensor_str[512];
    format_sensor_data_string(sensor_str, sizeof(sensor_str), &sensor_data);

    // Fetch weather and air quality data
    char weather_json[4096] = {0};
    char air_quality_json[2048] = {0};
    environmental_report_fetch_weather_data(weather_json, sizeof(weather_json),
                                            air_quality_json, sizeof(air_quality_json));

    char weather_str[256];
    format_weather_summary(weather_str, sizeof(weather_str), weather_json);

    char air_quality_str[256];
    format_air_quality_summary(air_quality_str, sizeof(air_quality_str), air_quality_json);

    // Build prompt for LLM
    char prompt[2048];
    snprintf(prompt, sizeof(prompt),
             "You are a helpful environmental assistant. Please provide a brief, natural spoken summary of the current environmental conditions.\n\n"
             "Current Date and Time: %s\n\n"
             "%s\n\n"
             "%s\n\n"
             "%s\n\n"
             "Please provide a friendly, conversational summary (2-3 sentences) that:\n"
             "1. Mentions the current time and date\n"
             "2. Summarizes the indoor environmental conditions\n"
             "3. Compares indoor vs outdoor conditions if available\n"
             "4. Provides any relevant health or comfort recommendations\n"
             "Keep it concise and natural, as if speaking to someone.",
             time_str, sensor_str, weather_str, air_quality_str);

    ESP_LOGI(TAG, "Sending prompt to LLM...");

    // Get LLM response
    char llm_response[512];
    esp_err_t ret = gemini_llm(prompt, llm_response, sizeof(llm_response));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LLM response: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "LLM Response: %s", llm_response);

    // Pause wake word detection during TTS playback
    wake_word_manager_pause();

    // Use voice_assistant_test_tts which handles TTS streaming and playback
    ret = voice_assistant_test_tts(llm_response);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to speak response: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "✅ Environmental report spoken successfully");
    }

    // Resume wake word detection
    wake_word_manager_resume();

    return ret;
}
