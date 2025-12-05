/**
 * @file somnus_ble.h
 * @brief Somnus-compatible BLE UART service for onboarding.
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback invoked when the mobile app requests a Wi-Fi connection.
 *
 * This mirrors the behaviour of SomnusDevice's onboarding flow: the BLE
 * characteristic delivers the SSID, password, user token, and an indicator
 * for production mode. The callback should attempt to establish the connection
 * and return true on success.
 */
typedef bool (*somnus_ble_connect_wifi_cb_t)(const char *ssid,
                                             const char *password,
                                             const char *user_token,
                                             bool is_production,
                                             void *ctx);

/**
 * @brief Callback invoked when a device command is received via BLE.
 *
 * This callback is used to handle device commands (LED, SongChange, SetVolume, etc.)
 * that are sent via BLE. The payload is a JSON string that can be passed directly
 * to the action handler.
 *
 * @param payload JSON payload string (can be single action or action list)
 * @param ctx Context pointer passed during configuration
 * @return ESP_OK on success, error code on failure
 */
typedef esp_err_t (*somnus_ble_device_command_cb_t)(const char *payload, void *ctx);

/**
 * @brief Configuration for the Somnus BLE service.
 */
typedef struct {
    somnus_ble_connect_wifi_cb_t connect_cb; /**< Optional Wi-Fi connect handler. */
    void *connect_ctx;                       /**< Context pointer passed to @p connect_cb. */
    somnus_ble_device_command_cb_t device_command_cb; /**< Optional device command handler. */
    void *device_command_ctx;                /**< Context pointer passed to @p device_command_cb. */
} somnus_ble_config_t;

/**
 * @brief Start the Somnus BLE UART service.
 *
 * @param config Optional configuration (may be NULL).
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t somnus_ble_start(const somnus_ble_config_t *config);

/**
 * @brief Stop the Somnus BLE UART service and release resources.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t somnus_ble_stop(void);

/**
 * @brief Check if the Somnus BLE service is currently advertising/active.
 */
bool somnus_ble_is_running(void);

/**
 * @brief Check if a BLE client is currently connected.
 * @return true if connected, false otherwise.
 */
bool somnus_ble_is_connected(void);

/**
 * @brief Check if BLE is currently advertising.
 * @return true if advertising (running but not connected), false otherwise.
 */
bool somnus_ble_is_advertising(void);

/**
 * @brief Send a notification payload to the connected mobile app.
 *
 * Messages longer than the BLE MTU are chunked into 20-byte fragments to
 * remain compatible with the reference Somnus implementation.
 *
 * @param message Null-terminated string to transmit.
 * @return ESP_OK if notification was queued for delivery, ESP_ERR_INVALID_STATE
 *         if no client is subscribed, or another esp_err_t on failure.
 */
esp_err_t somnus_ble_notify(const char *message);

/**
 * @brief Get BLE log entries for web interface
 * 
 * @param out_json Buffer to receive JSON string (caller must free)
 * @param max_entries Maximum number of entries to return (0 = all)
 * @return ESP_OK on success
 */
esp_err_t somnus_ble_get_logs(char **out_json, size_t max_entries);

#ifdef __cplusplus
}
#endif

