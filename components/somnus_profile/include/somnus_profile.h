/**
 * @file somnus_profile.h
 * @brief Somnus compatibility surface (BLE + AWS IoT) shared constants/utilities.
 *
 * This module mirrors the key identifiers and topic conventions used by the
 * reference SomnusDevice implementation so the ESP32 firmware exposes an
 * identical BLE/MQTT interface to mobile and cloud services.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Nordic UART-style GATT service UUIDs used by Somnus mobile apps. */
#define SOMNUS_UART_SERVICE_UUID          "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define SOMNUS_UART_RX_CHARACTERISTIC_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define SOMNUS_UART_TX_CHARACTERISTIC_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

/** BLE advertising local name expected by the Somnus mobile client. */
#define SOMNUS_BLE_LOCAL_NAME "rpi-gatt-server"

/** AWS IoT Core endpoint and default MQTT port used by SomnusDevice. */
#define SOMNUS_AWS_ENDPOINT "a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com"
#define SOMNUS_AWS_PORT     8883

/** Prefix applied to the device MAC (hex, uppercase, no separators). */
#define SOMNUS_DEVICE_ID_PREFIX "SOMNUS_"

/**
 * @brief Compute the Somnus-formatted device ID for this ESP32.
 *
 * Device IDs are composed of the literal prefix "SOMNUS_" followed by the
 * station MAC address rendered as 12 uppercase hexadecimal characters.
 *
 * @param[out] out Buffer to receive the null-terminated device ID string.
 * @param[out] out_len Length of @p out in bytes.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on bad parameters,
 *         ESP_ERR_INVALID_SIZE if the buffer is too small,
 *         or an ESP-IDF error from esp_read_mac().
 */
esp_err_t somnus_profile_get_device_id(char *out, size_t out_len);

/**
 * @brief Compose the Somnus MQTT subscribe and log topics for this device.
 *
 * Topics follow the patterns:
 *   subscribe: device/somnus/{DEVICE_ID}
 *   log:       device/receive/uat/{DEVICE_ID}
 *
 * @param[out] subscribe Buffer for the subscribe topic (null-terminated).
 * @param[in]  subscribe_len Length of the subscribe buffer.
 * @param[out] log Buffer for the log topic (null-terminated).
 * @param[in]  log_len Length of the log buffer.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid pointers,
 *         ESP_ERR_INVALID_SIZE if buffers are too small, or propagated error
 *         from somnus_profile_get_device_id().
 */
esp_err_t somnus_profile_get_topics(char *subscribe,
                                    size_t subscribe_len,
                                    char *log,
                                    size_t log_len);

/**
 * @brief Compose the Somnus telemetry topic for this device.
 *
 * Topic pattern: device/telemetry/{DEVICE_ID}
 *
 * @param[out] telemetry Buffer for the telemetry topic (null-terminated).
 * @param[in]  telemetry_len Length of the telemetry buffer.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid pointers,
 *         ESP_ERR_INVALID_SIZE if buffer is too small, or propagated error
 *         from somnus_profile_get_device_id().
 */
esp_err_t somnus_profile_get_telemetry_topic(char *telemetry, size_t telemetry_len);

/**
 * @brief Format a Somnus MQTT log payload into the provided buffer.
 *
 * Output matches the JSON structure produced by SomnusDevice:
 * {
 *   "Action": "Log",
 *   "Data": {
 *     "DeviceId": "<DEVICE_ID>",
 *     "LogName": "<log_stage>",
 *     "LogType": "<level>",
 *     "LogText": "<message>"
 *   }
 * }
 *
 * @param level     Log severity string (e.g. "INFO").
 * @param stage     Stage grouping ("Onboarding" or "AfterOnboarding").
 * @param message   Log text.
 * @param[out] out  Destination buffer for the JSON payload.
 * @param[in] out_len Size of @p out in bytes.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for bad pointers,
 *         ESP_ERR_INVALID_SIZE if the buffer is insufficient, otherwise
 *         propagated error from somnus_profile_get_device_id().
 */
esp_err_t somnus_profile_format_log_payload(const char *level,
                                            const char *stage,
                                            const char *message,
                                            char *out,
                                            size_t out_len);

#ifdef __cplusplus
}
#endif

