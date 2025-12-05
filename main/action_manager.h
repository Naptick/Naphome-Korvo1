#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "led_strip.h"  // For led_strip_handle_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Action types matching SomnusDevice action_manager.py
 */
typedef enum {
    ACTION_SONG_CHANGE = 0,
    ACTION_SPEECH,
    ACTION_LED,
    ACTION_SET_LED_INTENSITY,
    ACTION_SET_VOLUME,
    ACTION_PAUSE,
    ACTION_PLAY,
    ACTION_ROUTINE_END,
    ACTION_UNKNOWN
} action_type_t;

/**
 * Action data structure
 */
typedef struct {
    action_type_t type;
    union {
        struct {
            char song_name[128];
            float volume;
            int duration;  // seconds, -1 for indefinite
        } song_change;
        struct {
            char text[512];
        } speech;
        struct {
            // LED pattern data - JSON string representation
            char pattern_data[512];
        } led;
        struct {
            float intensity;  // 0.0 to 1.0
        } led_intensity;
        struct {
            float volume;  // 0.0 to 1.0
        } volume;
    } data;
} action_t;

/**
 * Device state
 */
typedef struct {
    bool paused;
    float current_volume;
    float current_led_intensity;
    bool audio_playing;
} device_state_t;

/**
 * Initialize action manager
 * @return ESP_OK on success
 */
esp_err_t action_manager_init(void);

/**
 * Execute a single action
 * @param action: Action to execute
 * @return ESP_OK on success
 */
esp_err_t action_manager_execute(const action_t *action);

/**
 * Execute action from JSON string (for LLM function calls)
 * @param action_json: JSON string with action data
 * @return ESP_OK on success
 */
esp_err_t action_manager_execute_json(const char *action_json);

/**
 * Get current device state
 * @param state: Output state structure
 * @return ESP_OK on success
 */
esp_err_t action_manager_get_state(device_state_t *state);

/**
 * Reset device to default state
 * @return ESP_OK on success
 */
esp_err_t action_manager_reset(void);

/**
 * Set LED strip handle (must be called after LED strip initialization)
 * @param strip: LED strip handle
 */
void action_manager_set_led_strip(led_strip_handle_t strip);

/**
 * Deinitialize action manager
 */
void action_manager_deinit(void);

#ifdef __cplusplus
}
#endif
