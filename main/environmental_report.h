#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fetch weather and air quality data from external API
 * @param weather_json: Buffer to store weather JSON response (can be NULL)
 * @param weather_json_len: Size of weather_json buffer
 * @param air_quality_json: Buffer to store air quality JSON response (can be NULL)
 * @param air_quality_json_len: Size of air_quality_json buffer
 * @return ESP_OK on success
 */
esp_err_t environmental_report_fetch_weather_data(char *weather_json, size_t weather_json_len,
                                                   char *air_quality_json, size_t air_quality_json_len);

/**
 * @brief Generate and speak environmental report using LLM and TTS
 * Collects sensor data, fetches weather/air quality, formats prompt for LLM,
 * gets response, and speaks it via TTS
 * @return ESP_OK on success
 */
esp_err_t environmental_report_generate_and_speak(void);

#ifdef __cplusplus
}
#endif
