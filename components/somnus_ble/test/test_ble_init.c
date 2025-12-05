/**
 * @file test_ble_init.c
 * @brief Unit tests for BLE initialization
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "somnus_ble.h"

static const char *TAG = "test_ble_init";

void setUp(void)
{
    // Initialize NVS (required for BLE)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized for test");
}

void tearDown(void)
{
    // Clean up BLE if it was started
    somnus_ble_stop();
    ESP_LOGI(TAG, "Test teardown complete");
}

/**
 * @brief Test basic BLE initialization with zero config
 */
void test_ble_init_zero_config(void)
{
    ESP_LOGI(TAG, "Test: BLE init with zero config");
    
    somnus_ble_config_t config = {0};
    esp_err_t err = somnus_ble_start(&config);
    
    TEST_ASSERT_EQUAL(ESP_OK, err);
    ESP_LOGI(TAG, "✓ BLE started successfully with zero config");
    
    // Give BLE time to initialize
    vTaskDelay(pdMS_TO_TICKS(500));
}

/**
 * @brief Test BLE initialization with NULL config (should fail)
 */
void test_ble_init_null_config(void)
{
    ESP_LOGI(TAG, "Test: BLE init with NULL config");
    
    esp_err_t err = somnus_ble_start(NULL);
    
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    ESP_LOGI(TAG, "✓ BLE correctly rejected NULL config");
}

/**
 * @brief Test BLE double initialization (should handle gracefully)
 */
void test_ble_double_init(void)
{
    ESP_LOGI(TAG, "Test: BLE double initialization");
    
    somnus_ble_config_t config = {0};
    
    // First init
    esp_err_t err1 = somnus_ble_start(&config);
    TEST_ASSERT_EQUAL(ESP_OK, err1);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Second init (should either succeed or fail gracefully)
    esp_err_t err2 = somnus_ble_start(&config);
    // Either OK (idempotent) or INVALID_STATE (already initialized) is acceptable
    TEST_ASSERT_TRUE(err2 == ESP_OK || err2 == ESP_ERR_INVALID_STATE);
    ESP_LOGI(TAG, "✓ BLE handled double init: %s", esp_err_to_name(err2));
}

/**
 * @brief Test BLE stop after init
 */
void test_ble_stop_after_init(void)
{
    ESP_LOGI(TAG, "Test: BLE stop after init");
    
    somnus_ble_config_t config = {0};
    esp_err_t err = somnus_ble_start(&config);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Stop BLE
    somnus_ble_stop();
    ESP_LOGI(TAG, "✓ BLE stopped successfully");
    
    // Should be able to start again
    err = somnus_ble_start(&config);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    ESP_LOGI(TAG, "✓ BLE restarted successfully");
}

/**
 * @brief Test BLE init with WiFi callback
 */
void test_ble_init_with_wifi_callback(void)
{
    ESP_LOGI(TAG, "Test: BLE init with WiFi callback");
    
    bool callback_called = false;
    
    somnus_ble_connect_wifi_cb_t wifi_cb = [](const char *ssid, const char *password, const char *token, bool is_prod, void *ctx) -> bool {
        bool *called = (bool *)ctx;
        *called = true;
        ESP_LOGI(TAG, "WiFi callback invoked (test): ssid=%s", ssid ? ssid : "NULL");
        return true; // Return success for test
    };
    
    somnus_ble_config_t config = {
        .connect_cb = wifi_cb,
        .connect_ctx = &callback_called
    };
    
    esp_err_t err = somnus_ble_start(&config);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    ESP_LOGI(TAG, "✓ BLE started with WiFi callback");
    
    vTaskDelay(pdMS_TO_TICKS(500));
    // Callback shouldn't be called just from init
    TEST_ASSERT_FALSE(callback_called);
}

/**
 * @brief Test BLE init with device command callback
 */
void test_ble_init_with_device_callback(void)
{
    ESP_LOGI(TAG, "Test: BLE init with device command callback");
    
    bool callback_called = false;
    
    somnus_ble_device_command_cb_t device_cb = [](const char *command, void *ctx) {
        bool *called = (bool *)ctx;
        *called = true;
        ESP_LOGI(TAG, "Device command callback invoked (test)");
        return ESP_OK;
    };
    
    somnus_ble_config_t config = {
        .device_command_cb = device_cb,
        .device_command_ctx = &callback_called
    };
    
    esp_err_t err = somnus_ble_start(&config);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    ESP_LOGI(TAG, "✓ BLE started with device command callback");
    
    vTaskDelay(pdMS_TO_TICKS(500));
    // Callback shouldn't be called just from init
    TEST_ASSERT_FALSE(callback_called);
}

void app_main(void)
{
    // Wait a bit for serial output to initialize
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "\n\n=== BLE Initialization Unit Tests ===\n");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_ble_init_zero_config);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    RUN_TEST(test_ble_init_null_config);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    RUN_TEST(test_ble_double_init);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    RUN_TEST(test_ble_stop_after_init);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    RUN_TEST(test_ble_init_with_wifi_callback);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    RUN_TEST(test_ble_init_with_device_callback);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    UNITY_END();
    
    ESP_LOGI(TAG, "\n=== All BLE Init Tests Complete ===\n");
    
    // Keep running so we can see results
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
