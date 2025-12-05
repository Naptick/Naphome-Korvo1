/**
 * @file somnus_profile.c
 */

#include "somnus_profile.h"

#include <stdio.h>
#include <string.h>

#include "esp_mac.h"

#define SOMNUS_DEVICE_ID_BODY_LEN 12

static esp_err_t somnus_profile_compose_device_id(char *out,
                                                  size_t out_len,
                                                  const uint8_t mac[6])
{
    if (!out || out_len == 0 || !mac) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t prefix_len = strlen(SOMNUS_DEVICE_ID_PREFIX);
    const size_t required = prefix_len + SOMNUS_DEVICE_ID_BODY_LEN + 1; /* +1 for NUL */
    if (out_len < required) {
        return ESP_ERR_INVALID_SIZE;
    }

    int written = snprintf(out,
                           out_len,
                           "%s%02X%02X%02X%02X%02X%02X",
                           SOMNUS_DEVICE_ID_PREFIX,
                           mac[0],
                           mac[1],
                           mac[2],
                           mac[3],
                           mac[4],
                           mac[5]);
    if (written < 0 || (size_t)written >= out_len) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t somnus_profile_get_device_id(char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        return err;
    }

    return somnus_profile_compose_device_id(out, out_len, mac);
}

esp_err_t somnus_profile_get_topics(char *subscribe,
                                    size_t subscribe_len,
                                    char *log,
                                    size_t log_len)
{
    if (!subscribe || !log) {
        return ESP_ERR_INVALID_ARG;
    }

    char device_id[SOMNUS_DEVICE_ID_BODY_LEN + sizeof(SOMNUS_DEVICE_ID_PREFIX)] = {0};
    esp_err_t err = somnus_profile_get_device_id(device_id, sizeof(device_id));
    if (err != ESP_OK) {
        return err;
    }

    int sub_written = snprintf(subscribe,
                               subscribe_len,
                               "device/somnus/%s",
                               device_id);
    if (sub_written < 0 || (size_t)sub_written >= subscribe_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    int log_written = snprintf(log,
                               log_len,
                               "device/receive/uat/%s",
                               device_id);
    if (log_written < 0 || (size_t)log_written >= log_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t somnus_profile_get_telemetry_topic(char *telemetry, size_t telemetry_len)
{
    if (!telemetry) {
        return ESP_ERR_INVALID_ARG;
    }

    char device_id[SOMNUS_DEVICE_ID_BODY_LEN + sizeof(SOMNUS_DEVICE_ID_PREFIX)] = {0};
    esp_err_t err = somnus_profile_get_device_id(device_id, sizeof(device_id));
    if (err != ESP_OK) {
        return err;
    }

    int written = snprintf(telemetry,
                           telemetry_len,
                           "device/telemetry/%s",
                           device_id);
    if (written < 0 || (size_t)written >= telemetry_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t somnus_profile_format_log_payload(const char *level,
                                            const char *stage,
                                            const char *message,
                                            char *out,
                                            size_t out_len)
{
    if (!level || !stage || !message || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    char device_id[SOMNUS_DEVICE_ID_BODY_LEN + sizeof(SOMNUS_DEVICE_ID_PREFIX)] = {0};
    esp_err_t err = somnus_profile_get_device_id(device_id, sizeof(device_id));
    if (err != ESP_OK) {
        return err;
    }

    int written = snprintf(out,
                           out_len,
                           "{\"Action\":\"Log\",\"Data\":{\"DeviceId\":\"%s\",\"LogName\":\"%s\",\"LogType\":\"%s\",\"LogText\":\"%s\"}}",
                           device_id,
                           stage,
                           level,
                           message);
    if (written < 0 || (size_t)written >= out_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

