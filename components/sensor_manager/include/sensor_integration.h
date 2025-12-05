#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sensor integration data structure
 * Based on Naptick BOM: CM1106S (CO2), PM2012 (PM+VOC), SHT4x (T/H), TSL2561 (Light), AS7341 (UV), VEML7700 (Ambient)
 */
typedef struct {
    // Temperature & Humidity (Sensirion SHT4x)
    float temperature_c;        ///< Temperature from SHT4x (°C)
    float humidity_rh;          ///< Humidity from SHT4x (%)
    bool sht4x_available;       ///< SHT4x sensor available
    
    // CO2 (Cubic CM1106S)
    float co2_ppm;              ///< CO2 from CM1106S (ppm)
    bool cm1106s_available;     ///< CM1106S sensor available
    
    // PM + VOC (Cubic PM2012)
    float pm1_0_ug_m3;          ///< PM1.0 from PM2012 (μg/m³)
    float pm2_5_ug_m3;          ///< PM2.5 from PM2012 (μg/m³)
    float pm10_ug_m3;           ///< PM10 from PM2012 (μg/m³)
    uint16_t voc_index;         ///< VOC index from PM2012
    bool pm2012_available;      ///< PM2012 sensor available
    
    // Light sensors
    uint16_t light_lux;         ///< Light from TSL2561 (lux)
    bool tsl2561_available;    ///< TSL2561 sensor available
    uint16_t uv_index;          ///< UV index from AS7341
    bool as7341_available;      ///< AS7341 sensor available
    uint16_t ambient_lux;       ///< Ambient light from VEML7700 (lux)
    bool veml7700_available;   ///< VEML7700 sensor available
    
    uint32_t last_update_ms;    ///< Last update timestamp (ms)
} sensor_integration_data_t;

/**
 * @brief Initialize sensor integration system
 * @return ESP_OK on success
 */
esp_err_t sensor_integration_init(void);

/**
 * @brief Start sensor sampling at 1Hz
 * @return ESP_OK on success
 */
esp_err_t sensor_integration_start(void);

/**
 * @brief Stop sensor sampling
 * @return ESP_OK on success
 */
esp_err_t sensor_integration_stop(void);

/**
 * @brief Get current sensor data
 * @return Sensor data structure
 */
sensor_integration_data_t sensor_integration_get_data(void);

#ifdef __cplusplus
}
#endif
