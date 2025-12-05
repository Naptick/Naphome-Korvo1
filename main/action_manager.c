#include "action_manager.h"
#include "audio_player.h"
#include "led_strip.h"
#include "led_indicators.h"
#include "gemini_api.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "action_manager";

// Global device state
static device_state_t s_device_state = {
    .paused = false,
    .current_volume = 1.0f,
    .current_led_intensity = 0.3f,
    .audio_playing = false
};

// LED strip handle (set externally)
static led_strip_handle_t s_led_strip = NULL;

// Paused state storage
static char s_paused_led_pattern[512] = {0};
static bool s_has_paused_led_state = false;

esp_err_t action_manager_init(void)
{
    s_device_state.paused = false;
    s_device_state.current_volume = 1.0f;
    s_device_state.current_led_intensity = 0.3f;
    s_device_state.audio_playing = false;
    s_has_paused_led_state = false;
    memset(s_paused_led_pattern, 0, sizeof(s_paused_led_pattern));
    
    ESP_LOGI(TAG, "Action manager initialized");
    return ESP_OK;
}

void action_manager_set_led_strip(led_strip_handle_t strip)
{
    s_led_strip = strip;
}

// Helper to parse action type from string
static action_type_t parse_action_type(const char *type_str)
{
    if (!type_str) return ACTION_UNKNOWN;
    
    if (strcmp(type_str, "SongChange") == 0) return ACTION_SONG_CHANGE;
    if (strcmp(type_str, "Speech") == 0) return ACTION_SPEECH;
    if (strcmp(type_str, "LED") == 0) return ACTION_LED;
    if (strcmp(type_str, "SetLEDIntensity") == 0) return ACTION_SET_LED_INTENSITY;
    if (strcmp(type_str, "SetVolume") == 0) return ACTION_SET_VOLUME;
    if (strcmp(type_str, "Pause") == 0) return ACTION_PAUSE;
    if (strcmp(type_str, "Play") == 0) return ACTION_PLAY;
    if (strcmp(type_str, "RoutineEnd") == 0) return ACTION_ROUTINE_END;
    
    return ACTION_UNKNOWN;
}

// Apply LED intensity scaling to RGB values
static uint8_t apply_led_intensity(uint8_t value)
{
    return (uint8_t)(value * s_device_state.current_led_intensity);
}

// Set LED pattern from JSON data
static esp_err_t execute_led_pattern(const char *pattern_json)
{
    if (!pattern_json) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_led_strip) {
        ESP_LOGW(TAG, "LED strip not initialized, skipping LED pattern");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Parse JSON pattern
    cJSON *json = cJSON_Parse(pattern_json);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse LED pattern JSON");
        return ESP_ERR_INVALID_ARG;
    }
    
    // For now, support simple patterns:
    // {"color": [r, g, b]} - solid color
    // {"pattern": "rainbow"} - rainbow pattern
    // {"pattern": "clear"} - clear LEDs
    
    cJSON *color = cJSON_GetObjectItem(json, "color");
    cJSON *pattern = cJSON_GetObjectItem(json, "pattern");
    
    if (color && cJSON_IsArray(color)) {
        // Solid color
        int r = 0, g = 0, b = 0;
        if (cJSON_GetArraySize(color) >= 3) {
            cJSON *r_item = cJSON_GetArrayItem(color, 0);
            cJSON *g_item = cJSON_GetArrayItem(color, 1);
            cJSON *b_item = cJSON_GetArrayItem(color, 2);
            if (r_item && cJSON_IsNumber(r_item)) r = r_item->valueint;
            if (g_item && cJSON_IsNumber(g_item)) g = g_item->valueint;
            if (b_item && cJSON_IsNumber(b_item)) b = b_item->valueint;
        }
        
        // Apply intensity and set all LEDs
        for (uint32_t i = 0; i < CONFIG_LED_AUDIO_LED_COUNT; i++) {
            led_strip_set_pixel(s_led_strip, i,
                               apply_led_intensity(r),
                               apply_led_intensity(g),
                               apply_led_intensity(b));
        }
        led_strip_refresh(s_led_strip);
        ESP_LOGI(TAG, "LED pattern: solid color RGB(%d, %d, %d)", r, g, b);
    } else if (pattern && cJSON_IsString(pattern)) {
        const char *pattern_str = pattern->valuestring;
        if (strcmp(pattern_str, "clear") == 0) {
            led_strip_clear(s_led_strip);
            ESP_LOGI(TAG, "LED pattern: clear");
        } else if (strcmp(pattern_str, "rainbow") == 0) {
            // Simple rainbow pattern
            for (uint32_t i = 0; i < CONFIG_LED_AUDIO_LED_COUNT; i++) {
                float hue = (float)i / CONFIG_LED_AUDIO_LED_COUNT * 360.0f;
                // Simple HSV to RGB conversion
                float c = 1.0f;
                float x = c * (1.0f - fabsf(fmodf(hue / 60.0f, 2.0f) - 1.0f));
                float m = 0.0f;
                float r = 0, g = 0, b = 0;
                
                if (hue < 60) { r = c; g = x; b = 0; }
                else if (hue < 120) { r = x; g = c; b = 0; }
                else if (hue < 180) { r = 0; g = c; b = x; }
                else if (hue < 240) { r = 0; g = x; b = c; }
                else if (hue < 300) { r = x; g = 0; b = c; }
                else { r = c; g = 0; b = x; }
                
                led_strip_set_pixel(s_led_strip, i,
                                   apply_led_intensity((uint8_t)(r * 255)),
                                   apply_led_intensity((uint8_t)(g * 255)),
                                   apply_led_intensity((uint8_t)(b * 255)));
            }
            led_strip_refresh(s_led_strip);
            ESP_LOGI(TAG, "LED pattern: rainbow");
        }
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

// Set volume (0.0 to 1.0)
static esp_err_t set_audio_volume(float volume)
{
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    
    s_device_state.current_volume = volume;
    
    // Note: ES8311 volume control would go here
    // For now, we just store the volume level
    // Actual implementation would write to ES8311 registers
    
    ESP_LOGI(TAG, "Volume set to %.2f", volume);
    return ESP_OK;
}

esp_err_t action_manager_execute(const action_t *action)
{
    if (!action) {
        return ESP_ERR_INVALID_ARG;
    }
    
    switch (action->type) {
        case ACTION_SONG_CHANGE: {
            // Note: Audio file playback not implemented yet
            // For now, we can use TTS to speak the song name
            ESP_LOGW(TAG, "SongChange not fully implemented - would play: %s", 
                     action->data.song_change.song_name);
            char msg[256];
            snprintf(msg, sizeof(msg), "Playing %s", action->data.song_change.song_name);
            // Could trigger TTS here
            return ESP_OK;
        }
        
        case ACTION_SPEECH: {
            // Use TTS to speak the text
            // This will be handled by the voice assistant
            ESP_LOGI(TAG, "Speech action: %s", action->data.speech.text);
            return ESP_OK;
        }
        
        case ACTION_LED: {
            return execute_led_pattern(action->data.led.pattern_data);
        }
        
        case ACTION_SET_LED_INTENSITY: {
            float intensity = action->data.led_intensity.intensity;
            if (intensity < 0.0f) intensity = 0.0f;
            if (intensity > 1.0f) intensity = 1.0f;
            s_device_state.current_led_intensity = intensity;
            ESP_LOGI(TAG, "LED intensity set to %.2f", intensity);
            return ESP_OK;
        }
        
        case ACTION_SET_VOLUME: {
            return set_audio_volume(action->data.volume.volume);
        }
        
        case ACTION_PAUSE: {
            if (s_device_state.paused) {
                return ESP_OK;  // Already paused
            }
            
            // Store current LED state
            if (s_led_strip) {
                strncpy(s_paused_led_pattern, "{\"pattern\":\"clear\"}", 
                       sizeof(s_paused_led_pattern) - 1);
                s_has_paused_led_state = true;
                led_strip_clear(s_led_strip);
            }
            
            s_device_state.paused = true;
            s_device_state.audio_playing = false;
            ESP_LOGI(TAG, "Device paused");
            return ESP_OK;
        }
        
        case ACTION_PLAY: {
            if (!s_device_state.paused) {
                return ESP_OK;  // Not paused
            }
            
            // Restore LED state if available
            if (s_has_paused_led_state && s_led_strip) {
                execute_led_pattern(s_paused_led_pattern);
                s_has_paused_led_state = false;
            }
            
            s_device_state.paused = false;
            ESP_LOGI(TAG, "Device resumed");
            return ESP_OK;
        }
        
        case ACTION_ROUTINE_END: {
            // Clear LEDs and reset state
            if (s_led_strip) {
                led_strip_clear(s_led_strip);
            }
            ESP_LOGI(TAG, "Routine ended");
            return ESP_OK;
        }
        
        default:
            ESP_LOGE(TAG, "Unknown action type: %d", action->type);
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t action_manager_execute_json(const char *action_json)
{
    if (!action_json) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *json = cJSON_Parse(action_json);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse action JSON");
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *action_type = cJSON_GetObjectItem(json, "Action");
    if (!action_type || !cJSON_IsString(action_type)) {
        ESP_LOGE(TAG, "Missing or invalid 'Action' field");
        cJSON_Delete(json);
        return ESP_ERR_INVALID_ARG;
    }
    
    action_t action = {0};
    action.type = parse_action_type(action_type->valuestring);
    
    cJSON *data = cJSON_GetObjectItem(json, "Data");
    
    switch (action.type) {
        case ACTION_SONG_CHANGE: {
            if (data) {
                cJSON *song_name = cJSON_GetObjectItem(data, "SongName");
                cJSON *volume = cJSON_GetObjectItem(data, "Volume");
                if (song_name && cJSON_IsString(song_name)) {
                    strncpy(action.data.song_change.song_name, song_name->valuestring,
                           sizeof(action.data.song_change.song_name) - 1);
                }
                if (volume && cJSON_IsNumber(volume)) {
                    action.data.song_change.volume = (float)volume->valuedouble;
                } else {
                    action.data.song_change.volume = 0.6f;
                }
            }
            break;
        }
        
        case ACTION_SPEECH: {
            if (data) {
                cJSON *text = cJSON_GetObjectItem(data, "Text");
                if (text && cJSON_IsString(text)) {
                    strncpy(action.data.speech.text, text->valuestring,
                           sizeof(action.data.speech.text) - 1);
                }
            }
            break;
        }
        
        case ACTION_LED: {
            if (data) {
                char *data_str = cJSON_PrintUnformatted(data);
                if (data_str) {
                    strncpy(action.data.led.pattern_data, data_str,
                           sizeof(action.data.led.pattern_data) - 1);
                    free(data_str);
                }
            }
            break;
        }
        
        case ACTION_SET_LED_INTENSITY: {
            if (data) {
                cJSON *intensity = cJSON_GetObjectItem(data, "Intensity");
                if (intensity && cJSON_IsNumber(intensity)) {
                    action.data.led_intensity.intensity = (float)intensity->valuedouble;
                }
            }
            break;
        }
        
        case ACTION_SET_VOLUME: {
            if (data) {
                cJSON *volume = cJSON_GetObjectItem(data, "Volume");
                if (volume && cJSON_IsNumber(volume)) {
                    action.data.volume.volume = (float)volume->valuedouble;
                }
            }
            break;
        }
        
        default:
            break;
    }
    
    esp_err_t ret = action_manager_execute(&action);
    cJSON_Delete(json);
    return ret;
}

esp_err_t action_manager_get_state(device_state_t *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(state, &s_device_state, sizeof(device_state_t));
    return ESP_OK;
}

esp_err_t action_manager_reset(void)
{
    s_device_state.paused = false;
    s_device_state.current_volume = 1.0f;
    s_device_state.current_led_intensity = 0.3f;
    s_device_state.audio_playing = false;
    s_has_paused_led_state = false;
    
    if (s_led_strip) {
        led_strip_clear(s_led_strip);
    }
    
    ESP_LOGI(TAG, "Device state reset");
    return ESP_OK;
}

void action_manager_deinit(void)
{
    action_manager_reset();
    ESP_LOGI(TAG, "Action manager deinitialized");
}
