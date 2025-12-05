#include "openwakeword_esp32.h"
#include "esp_log.h"
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/projdefs.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <cstdlib>
#include <cstring>
#include <cmath>

// For heap_caps_malloc
#ifndef MALLOC_CAP_SPIRAM
#define MALLOC_CAP_SPIRAM (1 << 6)
#endif
#ifndef MALLOC_CAP_8BIT
#define MALLOC_CAP_8BIT (1 << 0)
#endif

// Forward declaration - LED indicators will be linked from main component
extern "C" {
    void led_indicators_speech_detected(bool active);
    void led_indicators_wake_word_detected(void);
}

static const char *TAG = "openwakeword";

// OpenWakeWord integration
// Note: This is a placeholder implementation
// To use actual OpenWakeWord, you need to:
// 1. Add OpenWakeWord as a submodule or managed component
// 2. Include the OpenWakeWord headers
// 3. Link against the OpenWakeWord library

// For now, we'll create a stub that can be replaced with actual OpenWakeWord integration

struct openwakeword_context {
    uint32_t sample_rate;
    wake_word_callback_t callback;
    bool initialized;
    bool running;
    TaskHandle_t task_handle;
    QueueHandle_t audio_queue;
};

static openwakeword_context s_ctx = {
    .sample_rate = 0,
    .callback = nullptr,
    .initialized = false,
    .running = false,
    .task_handle = nullptr,
    .audio_queue = nullptr
};

// Placeholder for OpenWakeWord model
// In real implementation, this would be the actual model instance
// static void *s_model = nullptr;  // Unused - kept for potential future use

static void wake_word_task(void *pvParameters)
{
    openwakeword_context *ctx = (openwakeword_context *)pvParameters;
    // Buffer size: 512 samples = 32ms at 16kHz (matches queue chunk size)
    // Use PSRAM for audio buffer to save internal RAM
    const size_t buffer_size = 512;
    int16_t *audio_buffer = (int16_t *)heap_caps_malloc(buffer_size * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!audio_buffer) {
        // Fallback to internal RAM if PSRAM fails
        audio_buffer = (int16_t *)std::malloc(buffer_size * sizeof(int16_t));
    }
    
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Wake word detection task started");
    
    // Simple energy-based wake word detection (placeholder)
    // TODO: Replace with actual OpenWakeWord model inference
    static float energy_history[10] = {0};  // Keep last 10 chunks
    static int history_idx = 0;
    static int silence_count = 0;
    static int speech_count = 0;
    static int chunk_count = 0;
    static bool first_audio_detected = false;
    static bool was_silent = true;
    const float ENERGY_THRESHOLD = 5.0f;  // Lowered from 500.0 - RMS values are typically 1-15, silence ~1-2
    const int SPEECH_CHUNKS_REQUIRED = 3;   // ~96ms of speech (reduced from 5)
    const int SILENCE_CHUNKS_REQUIRED = 2;   // ~64ms of silence after speech (reduced from 3)
    
    ESP_LOGI(TAG, "Wake word detection task started (energy threshold: %.1f)", ENERGY_THRESHOLD);
    
    while (ctx->running) {
        // Wait for audio data from queue
        if (xQueueReceive(ctx->audio_queue, audio_buffer, pdMS_TO_TICKS(100)) == pdTRUE) {
            chunk_count++;
            
            // Calculate RMS energy of audio chunk
            const size_t chunk_size = 512;  // Match queue chunk size
            float sum_squares = 0.0f;
            int16_t max_sample = 0;
            int16_t min_sample = 0;
            for (size_t i = 0; i < chunk_size; i++) {
                int16_t sample = audio_buffer[i];
                if (sample > max_sample) max_sample = sample;
                if (sample < min_sample) min_sample = sample;
                float sample_f = (float)sample;
                sum_squares += sample_f * sample_f;
            }
            float rms_energy = std::sqrt(sum_squares / (float)chunk_size);
            energy_history[history_idx] = rms_energy;
            history_idx = (history_idx + 1) % 10;
            
            // Detect first audio input
            if (!first_audio_detected) {
                first_audio_detected = true;
                ESP_LOGI(TAG, "🎤 *** FIRST AUDIO DETECTED *** Chunk #%d: RMS=%.1f, peak=[%d, %d]",
                         chunk_count, rms_energy, min_sample, max_sample);
            }
            
            // Detect transition from silence to audio (any significant energy)
            bool has_audio = (std::abs(max_sample) > 100 || std::abs(min_sample) > 100);
            if (was_silent && has_audio) {
                ESP_LOGI(TAG, "🔊 *** AUDIO INPUT DETECTED *** Transition from silence to audio - RMS=%.1f, peak=[%d, %d]",
                         rms_energy, min_sample, max_sample);
                was_silent = false;
            } else if (!was_silent && !has_audio && rms_energy < 50.0f) {
                ESP_LOGI(TAG, "🔇 *** RETURNED TO SILENCE *** RMS=%.1f, peak=[%d, %d]",
                         rms_energy, min_sample, max_sample);
                was_silent = true;
            }
            
            // Log audio chunk details (every 50 chunks = ~1.6 seconds at 16kHz with 512-sample chunks)
            if (chunk_count % 50 == 0) {
                float avg_energy = 0.0f;
                for (int i = 0; i < 10; i++) {
                    avg_energy += energy_history[i];
                }
                avg_energy /= 10.0f;
                ESP_LOGI(TAG, "🎤 Audio chunk #%d: RMS=%.1f, peak=[%d, %d], avg_energy=%.1f, speech=%d, silence=%d",
                         chunk_count, rms_energy, min_sample, max_sample, avg_energy, speech_count, silence_count);
            } else {
                // More frequent debug for energy levels
                ESP_LOGD(TAG, "🎤 Chunk #%d: RMS=%.1f, peak=[%d, %d] %s",
                         chunk_count, rms_energy, min_sample, max_sample,
                         (rms_energy > ENERGY_THRESHOLD) ? "🔊 SPEECH" : "🔇 silence");
            }
            
            // Simple detection: speech followed by silence
            if (rms_energy > ENERGY_THRESHOLD) {
                if (speech_count == 0) {
                    ESP_LOGI(TAG, "🔊 *** SPEECH DETECTED *** (energy=%.1f > threshold=%.1f) - peak=[%d, %d]",
                             rms_energy, ENERGY_THRESHOLD, min_sample, max_sample);
                    // Show speech indicator (blue pulsing)
                    led_indicators_speech_detected(true);
                }
                speech_count++;
                if (speech_count > 0 && speech_count % 5 == 0) {
                    ESP_LOGI(TAG, "🔊 Speech continuing... %d chunks (~%dms)",
                             speech_count, (int)((speech_count * 512 * 1000) / ctx->sample_rate));
                }
                silence_count = 0;
            } else {
                if (speech_count > 0 && silence_count == 0) {
                    ESP_LOGI(TAG, "🔇 *** SILENCE AFTER SPEECH *** (energy=%.1f <= threshold=%.1f) - had %d speech chunks",
                             rms_energy, ENERGY_THRESHOLD, speech_count);
                    // Turn off speech indicator when silence detected
                    led_indicators_speech_detected(false);
                }
                silence_count++;
                if (speech_count >= SPEECH_CHUNKS_REQUIRED && silence_count >= SILENCE_CHUNKS_REQUIRED) {
                    // Potential wake word detected
                    ESP_LOGI(TAG, "✅ *** WAKE WORD DETECTED! *** (energy-based) - speech: %d chunks (~%dms), silence: %d chunks (~%dms)", 
                             speech_count, (int)((speech_count * 512 * 1000) / ctx->sample_rate),
                             silence_count, (int)((silence_count * 512 * 1000) / ctx->sample_rate));
                    
                    // Show wake word indicator (green flash)
                    led_indicators_wake_word_detected();
                    
                    if (ctx->callback) {
                        ctx->callback("hey_nap");
                    }
                    speech_count = 0;
                    silence_count = 0;
                }
            }
        } else {
            // Queue timeout - log occasionally to show we're still running
            if (chunk_count == 0 || chunk_count % 500 == 0) {
                ESP_LOGD(TAG, "Waiting for audio data... (chunk_count=%d)", chunk_count);
            }
        }
    }
    
    std::free(audio_buffer);
    ESP_LOGI(TAG, "Wake word detection task stopped");
    
    // Clear task handle before deleting self to prevent double-delete
    ctx->task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t openwakeword_init(uint32_t sample_rate, wake_word_callback_t callback)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "OpenWakeWord already initialized");
        return ESP_OK;
    }
    
    if (sample_rate != 16000) {
        ESP_LOGW(TAG, "OpenWakeWord typically uses 16kHz, got %" PRIu32 " Hz", sample_rate);
    }
    
    s_ctx.sample_rate = sample_rate;
    s_ctx.callback = callback;
    s_ctx.initialized = true;
    s_ctx.running = false;
    
    // Create audio queue - reduced size to save internal RAM
    // Queue size: 16 chunks of 512 samples each = 32ms per chunk, ~512ms total buffer
    // This prevents queue overflow during processing delays while conserving memory
    // Note: Using 512 samples to match mic capture task output (not 1024)
    const size_t queue_size = 16;  // Reduced from 64 to 16 to save internal RAM (64KB -> 16KB)
    const size_t chunk_size_samples = 512;  // 32ms at 16kHz (matches mic capture task)
    s_ctx.audio_queue = xQueueCreate(queue_size, chunk_size_samples * sizeof(int16_t));
    if (!s_ctx.audio_queue) {
        ESP_LOGE(TAG, "Failed to create audio queue");
        s_ctx.initialized = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Audio queue created: %zu chunks of %zu samples each (~%.1fms/chunk, ~%.1fs total buffer)",
             queue_size, chunk_size_samples, 
             (float)chunk_size_samples * 1000.0f / sample_rate,
             (float)(queue_size * chunk_size_samples) / sample_rate);
    
    // TODO: Load OpenWakeWord model
    // Example:
    // s_model = openwakeword_load_model("/spiffs/model.onnx");
    // if (!s_model) {
    //     ESP_LOGE(TAG, "Failed to load OpenWakeWord model");
    //     return ESP_ERR_NOT_FOUND;
    // }
    
    ESP_LOGI(TAG, "OpenWakeWord initialized (sample_rate=%" PRIu32 " Hz)", sample_rate);
    return ESP_OK;
}

esp_err_t openwakeword_process(const int16_t *audio_data, size_t num_samples)
{
    if (!s_ctx.initialized || !s_ctx.running) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!audio_data || num_samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    static size_t total_samples_processed = 0;
    total_samples_processed += num_samples;
    
    // Log every ~1 second of audio (16000 samples)
    if (total_samples_processed % 16000 < num_samples) {
        // Quick energy calculation for logging
        float sum_sq = 0.0f;
        for (size_t i = 0; i < num_samples && i < 512; i++) {
            float s = (float)audio_data[i];
            sum_sq += s * s;
        }
        float rms = std::sqrt(sum_sq / (float)num_samples);
        ESP_LOGD(TAG, "📥 Received %zu samples (total: %zu, ~%.1fs), RMS=%.1f",
                 num_samples, total_samples_processed, 
                 (float)total_samples_processed / s_ctx.sample_rate, rms);
    }
    
    // Send audio to processing task via queue
    // Process in chunks of 512 samples (32ms at 16kHz) - matches queue chunk size
    const size_t target_chunk_size = 512;
    size_t samples_processed = 0;
    
    while (samples_processed < num_samples) {
        size_t chunk_size = (num_samples - samples_processed > target_chunk_size) 
                          ? target_chunk_size 
                          : (num_samples - samples_processed);
        
        // Use stack allocation for small chunks (512 samples = 1KB)
        int16_t chunk[512];
        std::memcpy(chunk, audio_data + samples_processed, chunk_size * sizeof(int16_t));
        
        // Pad if needed
        if (chunk_size < target_chunk_size) {
            std::memset(chunk + chunk_size, 0, (target_chunk_size - chunk_size) * sizeof(int16_t));
        }
        
        // Send to queue (non-blocking)
        if (xQueueSend(s_ctx.audio_queue, chunk, 0) != pdTRUE) {
            // Queue full, drop this chunk
            ESP_LOGW(TAG, "⚠️  Audio queue full, dropping chunk (%zu samples)", chunk_size);
        }
        
        samples_processed += chunk_size;
    }
    
    return ESP_OK;
}

esp_err_t openwakeword_start(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_ctx.running) {
        return ESP_OK;
    }
    
    s_ctx.running = true;
    
    // Create wake word detection task
    xTaskCreate(
        wake_word_task,
        "wakeword",
        4096,
        &s_ctx,
        5,
        &s_ctx.task_handle
    );
    
    if (!s_ctx.task_handle) {
        ESP_LOGE(TAG, "Failed to create wake word task");
        s_ctx.running = false;
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Wake word detection started");
    return ESP_OK;
}

void openwakeword_stop(void)
{
    if (!s_ctx.running) {
        return;
    }
    
    s_ctx.running = false;
    
    // Wait for task to finish gracefully
    // The task will delete itself and clear the handle
    if (s_ctx.task_handle) {
        // Give task time to exit gracefully (it checks s_ctx.running and exits)
        // Wait up to 500ms for task to finish
        for (int i = 0; i < 10 && s_ctx.task_handle != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        
        // If handle is still set, task might be stuck - but don't try to delete
        // as it may have already deleted itself (handle would be invalid)
        // Just clear our reference
        if (s_ctx.task_handle != NULL) {
            ESP_LOGW(TAG, "Task handle still set after wait, clearing reference");
            s_ctx.task_handle = NULL;
        }
    }
    
    // Clear queue
    if (s_ctx.audio_queue) {
        xQueueReset(s_ctx.audio_queue);
    }
    
    ESP_LOGI(TAG, "Wake word detection stopped");
}

bool openwakeword_is_running(void)
{
    return s_ctx.running;
}

void openwakeword_deinit(void)
{
    openwakeword_stop();
    
    if (s_ctx.audio_queue) {
        vQueueDelete(s_ctx.audio_queue);
        s_ctx.audio_queue = NULL;
    }
    
    // TODO: Free OpenWakeWord model
    // if (s_model) {
    //     openwakeword_free_model(s_model);
    //     s_model = nullptr;
    // }
    
    std::memset(&s_ctx, 0, sizeof(s_ctx));
    ESP_LOGI(TAG, "OpenWakeWord deinitialized");
}
