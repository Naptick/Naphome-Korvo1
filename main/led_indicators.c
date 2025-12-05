#include "led_indicators.h"
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "led_indicators";
static led_strip_handle_t s_strip = NULL;
static bool s_speech_active = false;
static TaskHandle_t s_speech_task_handle = NULL;

// Apply brightness scaling to RGB values
static inline uint8_t apply_brightness(uint8_t value)
{
    return (uint16_t)value * CONFIG_LED_AUDIO_BRIGHTNESS / 255;
}

// Set a single LED pixel with brightness scaling
static void set_pixel_rgb(uint32_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip || index >= CONFIG_LED_AUDIO_LED_COUNT) {
        return;
    }
    led_strip_set_pixel(s_strip, index,
                        apply_brightness(r),
                        apply_brightness(g),
                        apply_brightness(b));
}

// Speech detection animation task (blue pulsing)
static void speech_indicator_task(void *pvParameters)
{
    const TickType_t delay = pdMS_TO_TICKS(50); // 20 Hz update rate
    uint32_t phase = 0;
    
    while (s_speech_active) {
        // Create pulsing blue effect
        // Use sine wave for smooth pulsing: brightness = 0.3 + 0.5 * sin(phase)
        // Simple linear pulse: phase goes from 0 to 628 (2*PI*100), brightness from 0.3 to 0.8
        float phase_norm = (float)(phase % 628) / 314.0f; // Normalize to 0-2
        float brightness = 0.3f + 0.5f * (1.0f + (phase_norm < 1.0f ? phase_norm : 2.0f - phase_norm));
        if (brightness > 1.0f) brightness = 1.0f;
        if (brightness < 0.3f) brightness = 0.3f;
        
        uint8_t blue = (uint8_t)(brightness * 255.0f);
        uint8_t green = (uint8_t)(brightness * 100.0f); // Slight green tint
        
        // Set all LEDs to blue (pulsing)
        for (uint32_t i = 0; i < CONFIG_LED_AUDIO_LED_COUNT; i++) {
            set_pixel_rgb(i, 0, green, blue);
        }
        led_strip_refresh(s_strip);
        
        phase = (phase + 5) % 628; // ~100 steps for full cycle (2*PI*100)
        vTaskDelay(delay);
    }
    
    // Turn off LEDs when speech ends
    led_strip_clear(s_strip);
    s_speech_task_handle = NULL;
    vTaskDelete(NULL);
}

void led_indicators_init(void)
{
    // LED strip handle should be set by app_main.c
    // This function is called after LED strip is initialized
    ESP_LOGI(TAG, "LED indicators initialized");
}

void led_indicators_speech_detected(bool active)
{
    if (active && !s_speech_active) {
        // Start speech indicator
        s_speech_active = true;
        if (s_speech_task_handle == NULL) {
            xTaskCreate(
                speech_indicator_task,
                "speech_led",
                2048,
                NULL,
                5,
                &s_speech_task_handle
            );
            ESP_LOGI(TAG, "🔵 Speech indicator started (blue pulsing)");
        }
    } else if (!active && s_speech_active) {
        // Stop speech indicator
        s_speech_active = false;
        // Task will exit on its own
        ESP_LOGI(TAG, "🔵 Speech indicator stopped");
    }
}

void led_indicators_wake_word_detected(void)
{
    ESP_LOGI(TAG, "🟢 Wake word indicator (green flash)");
    
    // Flash green 3 times quickly
    for (int flash = 0; flash < 3; flash++) {
        // Bright green
        for (uint32_t i = 0; i < CONFIG_LED_AUDIO_LED_COUNT; i++) {
            set_pixel_rgb(i, 0, 255, 0); // Green
        }
        led_strip_refresh(s_strip);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Off
        led_strip_clear(s_strip);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // Brief pause, then return to normal (or speech indicator if active)
    vTaskDelay(pdMS_TO_TICKS(100));
}

void led_indicators_clear(void)
{
    s_speech_active = false;
    if (s_speech_task_handle) {
        // Give task time to exit
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    led_strip_clear(s_strip);
}

// Function to set the LED strip handle (called from app_main.c)
void led_indicators_set_strip(led_strip_handle_t strip)
{
    s_strip = strip;
}
