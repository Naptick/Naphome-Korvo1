/**
 * @file sensor_integration.c
 * @brief Unified sensor integration - samples all I2C sensors at 1Hz
 */

#include "sensor_integration.h"

#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "driver/i2c.h"
// Compatibility layer for v5.0 API
#define i2c_master_bus_handle_t i2c_port_t
#define i2c_master_dev_handle_t i2c_cmd_handle_t
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

// Sensor drivers - TODO: Re-enable when component discovery is fixed
// #include "sht45.h"
// #include "sgp40.h"
// #include "scd40.h"
// #include "vcnl4040.h"
// #include "ec10.h"

// Sensor manager
#include "sensor_manager.h"

static const char *TAG = "sensor_integration";

// I2C configuration - Using UART pins (TXD/RXD) for sensor I2C bus
// ESP32-S3 UART0: TXD = GPIO43, RXD = GPIO44
#define I2C_MASTER_SCL_IO           43    // GPIO number for I2C master clock (TXD/UART0_TX)
#define I2C_MASTER_SDA_IO           44    // GPIO number for I2C master data (RXD/UART0_RX)
#define I2C_MASTER_NUM              0     // I2C master i2c port number
#define I2C_MASTER_FREQ_HZ          100000 // I2C master clock frequency
#define I2C_MASTER_TIMEOUT_MS       1000

// Sensor sampling rate: 1Hz = 1000ms (changed to 60s for publish interval)
#define SENSOR_SAMPLE_INTERVAL_MS   60000  // 60 seconds

// Sensor handles - TODO: Re-enable when sensor driver components are available
static i2c_master_bus_handle_t s_i2c_bus = I2C_NUM_MAX; // Use invalid port instead of NULL
// static sht45_handle_t s_sht45;
// static sgp40_t s_sgp40;
// static scd40_t s_scd40;
// static vcnl4040_t s_vcnl4040;
// static ec10_t s_ec10;

static bool s_initialized = false;
static bool s_running = false;
static TaskHandle_t s_task_handle = NULL;

// Sensor data cache (matching sensor_integration_data_t)
typedef struct {
    float temperature_c;
    float humidity_rh;
    bool sht4x_available;
    float co2_ppm;
    bool cm1106s_available;
    float pm1_0_ug_m3;
    float pm2_5_ug_m3;
    float pm10_ug_m3;
    uint16_t voc_index;
    bool pm2012_available;
    uint16_t light_lux;
    bool tsl2561_available;
    uint16_t uv_index;
    bool as7341_available;
    uint16_t ambient_lux;
    bool veml7700_available;
    uint32_t last_update_ms;
} sensor_data_cache_t;

static sensor_data_cache_t s_sensor_cache = {0};

static void sensor_sampling_task(void *arg);
static bool sample_sht4x_cb(cJSON *sensor_root);
static bool sample_cm1106s_cb(cJSON *sensor_root);
static bool sample_pm2012_cb(cJSON *sensor_root);
static bool sample_tsl2561_cb(cJSON *sensor_root);
static bool sample_as7341_cb(cJSON *sensor_root);
static bool sample_veml7700_cb(cJSON *sensor_root);

esp_err_t sensor_integration_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing sensor integration...");

    // TODO: Re-enable when sensor driver components are available
    // Initialize I2C master bus
    // i2c_master_bus_config_t i2c_bus_config = {
    //     .i2c_port = I2C_MASTER_NUM,
    //     .sda_io_num = I2C_MASTER_SDA_IO,
    //     .scl_io_num = I2C_MASTER_SCL_IO,
    //     .clk_source = I2C_CLK_SRC_DEFAULT,
    //     .glitch_ignore_cnt = 7,
    //     .flags = {
    //         .enable_internal_pullup = true,
    //     },
    // };
    // i2c_master_bus_handle_t bus_handle = I2C_NUM_MAX;
    // esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &bus_handle);
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(ret));
    //     return ret;
    // }
    // s_i2c_bus = bus_handle;
    
    // For now, mark all sensors as unavailable (will use synthetic data)
    // Always use synthetic data when sensors are not physically present
    s_sensor_cache.sht4x_available = false;
    s_sensor_cache.cm1106s_available = false;
    s_sensor_cache.pm2012_available = false;
    s_sensor_cache.tsl2561_available = false;
    s_sensor_cache.as7341_available = false;
    s_sensor_cache.veml7700_available = false;
    ESP_LOGI(TAG, "Sensor drivers disabled - using synthetic data (all sensors marked as synthetic)");

    // Initialize sensor manager with 1Hz sampling (1000ms)
    sensor_manager_config_t mgr_cfg = {
        .publish_interval_ms = SENSOR_SAMPLE_INTERVAL_MS,
    };
    sensor_manager_init(&mgr_cfg);

    // Register all sensors (based on Naptick BOM)
    sensor_manager_sensor_t sensors[] = {
        {.name = "sht4x", .sample_cb = sample_sht4x_cb},      // Sensirion SHT4x (Temp & Humidity)
        {.name = "cm1106s", .sample_cb = sample_cm1106s_cb},  // Cubic CM1106S (CO2)
        {.name = "pm2012", .sample_cb = sample_pm2012_cb},    // Cubic PM2012 (PM1.0+2.5+10+VOC)
        {.name = "tsl2561", .sample_cb = sample_tsl2561_cb},  // AMS TSL2561 (Light)
        {.name = "as7341", .sample_cb = sample_as7341_cb},   // AS7341 (UV Light)
        {.name = "veml7700", .sample_cb = sample_veml7700_cb}, // VISHAY VEML7700 (Ambient Light)
    };

    for (size_t i = 0; i < sizeof(sensors) / sizeof(sensors[0]); i++) {
        esp_err_t err = sensor_manager_register(&sensors[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register sensor %s", sensors[i].name);
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Sensor integration initialized - sampling at 1Hz");
    return ESP_OK;
}

esp_err_t sensor_integration_start(void)
{
    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(sensor_integration_init(), TAG, "init failed");
    }

    if (s_running) {
        return ESP_OK;
    }

    // Start sensor manager
    ESP_RETURN_ON_ERROR(sensor_manager_start(), TAG, "sensor manager start failed");

    // Start sampling task
    BaseType_t ret = xTaskCreate(sensor_sampling_task,
                                 "sensor_sampling",
                                 4096,
                                 NULL,
                                 5,
                                 &s_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sensor sampling task");
        return ESP_ERR_NO_MEM;
    }

    s_running = true;
    ESP_LOGI(TAG, "Sensor integration started");
    return ESP_OK;
}

esp_err_t sensor_integration_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    sensor_manager_stop();
    s_running = false;

    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }

    ESP_LOGI(TAG, "Sensor integration stopped");
    return ESP_OK;
}

sensor_integration_data_t sensor_integration_get_data(void)
{
    sensor_integration_data_t data = {0};
    data.temperature_c = s_sensor_cache.temperature_c;
    data.humidity_rh = s_sensor_cache.humidity_rh;
    data.sht4x_available = s_sensor_cache.sht4x_available;
    data.co2_ppm = s_sensor_cache.co2_ppm;
    data.cm1106s_available = s_sensor_cache.cm1106s_available;
    data.pm1_0_ug_m3 = s_sensor_cache.pm1_0_ug_m3;
    data.pm2_5_ug_m3 = s_sensor_cache.pm2_5_ug_m3;
    data.pm10_ug_m3 = s_sensor_cache.pm10_ug_m3;
    data.voc_index = s_sensor_cache.voc_index;
    data.pm2012_available = s_sensor_cache.pm2012_available;
    data.light_lux = s_sensor_cache.light_lux;
    data.tsl2561_available = s_sensor_cache.tsl2561_available;
    data.uv_index = s_sensor_cache.uv_index;
    data.as7341_available = s_sensor_cache.as7341_available;
    data.ambient_lux = s_sensor_cache.ambient_lux;
    data.veml7700_available = s_sensor_cache.veml7700_available;
    data.last_update_ms = s_sensor_cache.last_update_ms;
    return data;
}

static void sensor_sampling_task(void *arg)
{
    (void)arg;
    const TickType_t delay_ticks = pdMS_TO_TICKS(SENSOR_SAMPLE_INTERVAL_MS);
    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG, "Sensor sampling task started (1Hz)");

    while (s_running) {
        // TODO: Re-enable when sensor driver components are available
        // For now, generate synthetic sensor data
        static uint32_t sensor_synthetic_counter = 0;
        sensor_synthetic_counter++;
        
        // Generate synthetic SHT4x data (temperature, humidity) - Sensirion SHT4x
        s_sensor_cache.temperature_c = 22.0f + 3.0f * sinf(sensor_synthetic_counter * 0.01f);
        s_sensor_cache.humidity_rh = 50.0f + 10.0f * sinf(sensor_synthetic_counter * 0.008f);
        s_sensor_cache.sht4x_available = false;  // Always false = synthetic
        
        // Generate synthetic CM1106S data (CO2) - Cubic CM1106S
        s_sensor_cache.co2_ppm = 450.0f + 50.0f * sinf(sensor_synthetic_counter * 0.005f);
        s_sensor_cache.cm1106s_available = false;  // Always false = synthetic
        
        // Generate synthetic PM2012 data (PM1.0+2.5+10+VOC) - Cubic PM2012
        static uint32_t pm2012_synthetic_counter = 0;
        pm2012_synthetic_counter++;
        float base_pm = 15.0f + 5.0f * sinf(pm2012_synthetic_counter * 0.01f);
        s_sensor_cache.pm1_0_ug_m3 = base_pm * 0.7f;
        s_sensor_cache.pm2_5_ug_m3 = base_pm;
        s_sensor_cache.pm10_ug_m3 = base_pm * 1.5f;
        s_sensor_cache.voc_index = 100 + (uint16_t)(50.0f * sinf(sensor_synthetic_counter * 0.01f));
        s_sensor_cache.pm2012_available = false;  // Always false = synthetic
        
        // Generate synthetic TSL2561 data (Light) - AMS TSL2561
        s_sensor_cache.light_lux = 200 + (uint16_t)(100.0f * sinf(sensor_synthetic_counter * 0.02f));
        s_sensor_cache.tsl2561_available = false;  // Always false = synthetic
        
        // Generate synthetic AS7341 data (UV Light)
        s_sensor_cache.uv_index = 3 + (uint16_t)(2.0f * sinf(sensor_synthetic_counter * 0.015f));
        s_sensor_cache.as7341_available = false;  // Always false = synthetic
        
        // Generate synthetic VEML7700 data (Ambient Light) - VISHAY VEML7700
        s_sensor_cache.ambient_lux = 180 + (uint16_t)(80.0f * sinf(sensor_synthetic_counter * 0.018f));
        s_sensor_cache.veml7700_available = false;  // Always false = synthetic

        s_sensor_cache.last_update_ms = esp_log_timestamp();

        // Log sensor readings
        ESP_LOGI(TAG, "Sensors: T=%.1f°C H=%.1f%% CO2=%.0fppm PM[1.0/2.5/10]=[%.1f/%.1f/%.1f]μg/m³ VOC=%u Light=%u UV=%u Ambient=%u",
                 s_sensor_cache.temperature_c,
                 s_sensor_cache.humidity_rh,
                 s_sensor_cache.co2_ppm,
                 s_sensor_cache.pm1_0_ug_m3,
                 s_sensor_cache.pm2_5_ug_m3,
                 s_sensor_cache.pm10_ug_m3,
                 s_sensor_cache.voc_index,
                 s_sensor_cache.light_lux,
                 s_sensor_cache.uv_index,
                 s_sensor_cache.ambient_lux);

        vTaskDelayUntil(&last_wake, delay_ticks);
    }

    vTaskDelete(NULL);
}

// Sensor sampling callbacks (all return synthetic data)
static bool sample_sht4x_cb(cJSON *sensor_root)
{
    cJSON_AddNumberToObject(sensor_root, "temperature_c", s_sensor_cache.temperature_c);
    cJSON_AddNumberToObject(sensor_root, "humidity_rh", s_sensor_cache.humidity_rh);
    cJSON_AddBoolToObject(sensor_root, "synthetic", true);
    return true;
}

static bool sample_cm1106s_cb(cJSON *sensor_root)
{
    cJSON_AddNumberToObject(sensor_root, "co2_ppm", s_sensor_cache.co2_ppm);
    cJSON_AddBoolToObject(sensor_root, "synthetic", true);
    return true;
}

static bool sample_pm2012_cb(cJSON *sensor_root)
{
    cJSON_AddNumberToObject(sensor_root, "pm1_0_ug_m3", s_sensor_cache.pm1_0_ug_m3);
    cJSON_AddNumberToObject(sensor_root, "pm2_5_ug_m3", s_sensor_cache.pm2_5_ug_m3);
    cJSON_AddNumberToObject(sensor_root, "pm10_ug_m3", s_sensor_cache.pm10_ug_m3);
    cJSON_AddNumberToObject(sensor_root, "voc_index", s_sensor_cache.voc_index);
    cJSON_AddBoolToObject(sensor_root, "synthetic", true);
    return true;
}

static bool sample_tsl2561_cb(cJSON *sensor_root)
{
    cJSON_AddNumberToObject(sensor_root, "light_lux", s_sensor_cache.light_lux);
    cJSON_AddBoolToObject(sensor_root, "synthetic", true);
    return true;
}

static bool sample_as7341_cb(cJSON *sensor_root)
{
    cJSON_AddNumberToObject(sensor_root, "uv_index", s_sensor_cache.uv_index);
    cJSON_AddBoolToObject(sensor_root, "synthetic", true);
    return true;
}

static bool sample_veml7700_cb(cJSON *sensor_root)
{
    cJSON_AddNumberToObject(sensor_root, "ambient_lux", s_sensor_cache.ambient_lux);
    cJSON_AddBoolToObject(sensor_root, "synthetic", true);
    return true;
}
