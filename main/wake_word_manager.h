#pragma once

#include "esp_err.h"
#include "korvo1.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize wake word manager
 * This sets up microphone input and OpenWakeWord processing
 * @return ESP_OK on success
 */
esp_err_t wake_word_manager_init(void);

/**
 * Start wake word detection
 * @return ESP_OK on success
 */
esp_err_t wake_word_manager_start(void);

/**
 * Stop wake word detection
 */
void wake_word_manager_stop(void);

/**
 * Check if wake word detection is active
 * @return true if active
 */
bool wake_word_manager_is_active(void);

/**
 * Pause wake word detection (e.g., during audio playback to prevent feedback)
 */
void wake_word_manager_pause(void);

/**
 * Resume wake word detection after pausing
 */
void wake_word_manager_resume(void);

/**
 * Deinitialize wake word manager
 */
void wake_word_manager_deinit(void);

#ifdef __cplusplus
}
#endif
