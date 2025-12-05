#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Audio file information
 */
typedef struct {
    const char *name;        // Song name (without .mp3 extension)
    const char *display_name; // Display name for UI
    const uint8_t *data;     // MP3 file data (NULL if not embedded)
    size_t data_len;         // MP3 file data length
} audio_file_info_t;

/**
 * Initialize audio file manager
 * @return ESP_OK on success
 */
esp_err_t audio_file_manager_init(void);

/**
 * Get number of available audio files
 * @return Number of files
 */
size_t audio_file_manager_get_count(void);

/**
 * Get audio file info by index
 * @param index: File index (0 to count-1)
 * @param info: Output file info structure
 * @return ESP_OK on success
 */
esp_err_t audio_file_manager_get_by_index(size_t index, audio_file_info_t *info);

/**
 * Get audio file info by name
 * @param name: File name (with or without .mp3 extension)
 * @param info: Output file info structure
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found
 */
esp_err_t audio_file_manager_get_by_name(const char *name, audio_file_info_t *info);

/**
 * Play audio file by name
 * @param name: File name (with or without .mp3 extension)
 * @param volume: Volume level (0.0 to 1.0)
 * @param duration: Duration in seconds (-1 for full file)
 * @return ESP_OK on success
 */
esp_err_t audio_file_manager_play(const char *name, float volume, int duration);

/**
 * Stop current playback
 * @return ESP_OK on success
 */
esp_err_t audio_file_manager_stop(void);

/**
 * Check if audio is currently playing
 * @return true if playing
 */
bool audio_file_manager_is_playing(void);

/**
 * Get list of all available song names (for web UI)
 * @param names: Output array of song names (caller must free)
 * @param count: Output number of songs
 * @return ESP_OK on success
 */
esp_err_t audio_file_manager_get_all_names(char ***names, size_t *count);

#ifdef __cplusplus
}
#endif
