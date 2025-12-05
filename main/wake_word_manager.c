#include "wake_word_manager.h"
#include "openwakeword_esp32.h"
#include "voice_assistant.h"
#include "gemini_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "audio_codec_if.h"
#include "es7210_adc.h"
#include <string.h>
#include <math.h>

static const char *TAG = "wake_word_mgr";

// ES7210 codec device handle
static esp_codec_dev_handle_t s_es7210_dev = NULL;
static audio_codec_data_if_t *s_record_data_if = NULL;
static audio_codec_ctrl_if_t *s_record_ctrl_if = NULL;
static audio_codec_if_t *s_record_codec_if = NULL;
static i2s_chan_handle_t s_rx_handle = NULL;

static bool s_initialized = false;
static bool s_running = false;
static SemaphoreHandle_t s_i2s_mutex = NULL;  // Mutex to protect I2S channel access

// STT parallel processing
static QueueHandle_t s_stt_audio_queue = NULL;
static TaskHandle_t s_stt_task_handle = NULL;
static bool s_stt_enabled = true;  // Enable parallel STT by default
#define STT_QUEUE_SIZE 32  // Buffer ~1 second of audio (32 chunks * 512 samples = 16384 samples = 1.024s at 16kHz)
#define STT_CHUNK_SIZE 512  // Match mic capture chunk size
#define STT_BUFFER_DURATION_MS 2000  // Send to STT every 2 seconds
#define STT_BUFFER_SAMPLES (16000 * STT_BUFFER_DURATION_MS / 1000)  // 32000 samples = 2 seconds

// Old low-level register functions removed - now using esp_codec_dev high-level API

// Initialize ES7210 using esp_codec_dev high-level API (per ESP-SKAINET)
static esp_err_t es7210_init_with_codec_dev(void)
{
    ESP_LOGI(TAG, "Initializing ES7210 using esp_codec_dev API...");
    
    // Step 1: Configure I2S for ES7210 (I2S_NUM_1)
    // GPIO pins per ESP-SKAINET BSP: SCLK=GPIO10, LRCK=GPIO9, SDIN=GPIO11, MCLK=GPIO20
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_rx_handle), TAG, "create I2S channel failed");
    
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_20,  // MCLK
            .bclk = GPIO_NUM_10,  // BCLK
            .ws = GPIO_NUM_9,     // LRCK/WS
            .dout = I2S_GPIO_UNUSED,
            .din = GPIO_NUM_11,   // SDIN (data input)
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx_handle, &std_cfg), TAG, "init I2S std mode failed");
    // Enable I2S channel to start clocks BEFORE creating codec interfaces
    // ES7210 in slave mode needs BCLK/LRCK/MCLK from ESP32 to start outputting data
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_handle), TAG, "enable I2S channel failed");
    ESP_LOGI(TAG, "I2S channel configured and enabled (clocks running for ES7210 slave mode)");
    
    // Step 2: Create I2S data interface
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_1,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .rx_handle = s_rx_handle,
        .tx_handle = NULL,
#endif
    };
    s_record_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!s_record_data_if) {
        ESP_LOGE(TAG, "Failed to create I2S data interface");
        i2s_channel_disable(s_rx_handle);
        i2s_del_channel(s_rx_handle);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "I2S data interface created");
    
    // Step 3: Create I2C control interface
    // Note: ES7210_CODEC_DEFAULT_ADDR is 0x80 (8-bit address)
    // I2C port should be I2C_NUM_0 (same as ES8311, initialized by audio_player)
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,  // Use same I2C bus as ES8311
        .addr = ES7210_CODEC_DEFAULT_ADDR,  // 0x80 (8-bit address)
    };
    s_record_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!s_record_ctrl_if) {
        ESP_LOGE(TAG, "Failed to create I2C control interface");
        audio_codec_delete_data_if(s_record_data_if);
        i2s_channel_disable(s_rx_handle);
        i2s_del_channel(s_rx_handle);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "I2C control interface created (address 0x%02x)", ES7210_CODEC_DEFAULT_ADDR);
    
    // Step 4: Create ES7210 codec interface
    // Per ESP-SKAINET: only ctrl_if and mic_selected are needed (other fields use defaults)
    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = s_record_ctrl_if,
        .mic_selected = ES7120_SEL_MIC1 | ES7120_SEL_MIC2 | ES7120_SEL_MIC3 | ES7120_SEL_MIC4,
    };
    s_record_codec_if = es7210_codec_new(&es7210_cfg);
    if (!s_record_codec_if) {
        ESP_LOGE(TAG, "Failed to create ES7210 codec interface");
        audio_codec_delete_ctrl_if(s_record_ctrl_if);
        audio_codec_delete_data_if(s_record_data_if);
        i2s_channel_disable(s_rx_handle);
        i2s_del_channel(s_rx_handle);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ES7210 codec interface created (all 4 mics enabled)");
    
    // Step 5: Create codec device
    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = s_record_codec_if,
        .data_if = s_record_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    s_es7210_dev = esp_codec_dev_new(&dev_cfg);
    if (!s_es7210_dev) {
        ESP_LOGE(TAG, "Failed to create ES7210 codec device");
        audio_codec_delete_codec_if(s_record_codec_if);
        audio_codec_delete_ctrl_if(s_record_ctrl_if);
        audio_codec_delete_data_if(s_record_data_if);
        i2s_channel_disable(s_rx_handle);
        i2s_del_channel(s_rx_handle);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ES7210 codec device created");
    
    // Step 6: Open codec device with sample info
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,  // Stereo
        .bits_per_sample = 32,
    };
    esp_err_t ret = esp_codec_dev_open(s_es7210_dev, &fs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open ES7210 codec device: %s", esp_err_to_name(ret));
        esp_codec_dev_delete(s_es7210_dev);
        audio_codec_delete_codec_if(s_record_codec_if);
        audio_codec_delete_ctrl_if(s_record_ctrl_if);
        audio_codec_delete_data_if(s_record_data_if);
        i2s_channel_disable(s_rx_handle);
        i2s_del_channel(s_rx_handle);
        return ret;
    }
    ESP_LOGI(TAG, "ES7210 codec device opened: 16kHz, stereo, 32-bit");
    
    // Step 7: Set input channel gains (optional, per ESP-SKAINET)
    // ESP-SKAINET sets gains for channels 0, 1, and 3 (channel 2 is reference)
    const float RECORD_VOLUME = 0.0f;  // 0dB gain
    esp_codec_dev_set_in_channel_gain(s_es7210_dev, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0), RECORD_VOLUME);
    esp_codec_dev_set_in_channel_gain(s_es7210_dev, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1), RECORD_VOLUME);
    esp_codec_dev_set_in_channel_gain(s_es7210_dev, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(3), RECORD_VOLUME);
    ESP_LOGI(TAG, "ES7210 channel gains configured");
    
    // Step 8: Give ES7210 time to stabilize and start producing data
    // The codec needs a moment after opening to start ADC conversion
    // esp_codec_dev_open already enabled the I2S channel, so we don't need to enable it again
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Step 9: Try direct I2S channel read to see if data is available (bypassing esp_codec_dev)
    // This helps diagnose if the issue is with esp_codec_dev_read or the ES7210 itself
    int32_t test_buffer[16];
    size_t test_bytes_read = 0;
    esp_err_t i2s_ret = i2s_channel_read(s_rx_handle, test_buffer, sizeof(test_buffer), &test_bytes_read, 100);
    ESP_LOGI(TAG, "Direct I2S channel read: ret=%s, bytes_read=%zu", esp_err_to_name(i2s_ret), test_bytes_read);
    if (test_bytes_read > 0) {
        ESP_LOGI(TAG, "First 4 I2S samples: 0x%08x, 0x%08x, 0x%08x, 0x%08x", 
                 (unsigned int)test_buffer[0], (unsigned int)test_buffer[1],
                 (unsigned int)test_buffer[2], (unsigned int)test_buffer[3]);
    }
    
    // Step 10: Try esp_codec_dev_read to compare
    int test_read = esp_codec_dev_read(s_es7210_dev, test_buffer, sizeof(test_buffer));
    ESP_LOGI(TAG, "esp_codec_dev_read test: %d bytes", test_read);
    
    ESP_LOGI(TAG, "✅ ES7210 initialized successfully using esp_codec_dev API");
    return ESP_OK;
}

// Wake word detection callback
static void on_wake_word_detected(const char *wake_word)
{
    ESP_LOGI(TAG, "*** WAKE WORD DETECTED: %s ***", wake_word);
    
    // Only process if it's "Hey Nap"
    if (strcmp(wake_word, "hey_nap") != 0) {
        ESP_LOGD(TAG, "Ignoring wake word: %s (expected 'hey_nap')", wake_word);
        return;
    }
    
    // IMPORTANT: Temporarily stop the continuous mic capture task to avoid I2S conflicts
    // The continuous task and recording both try to read from the same I2S channel
    bool was_running = s_running;
    if (was_running) {
        ESP_LOGI(TAG, "Pausing continuous mic capture during recording...");
        s_running = false;  // Stop continuous capture
        // Wait longer for task to exit - it needs to finish current I2S read and break from loop
        vTaskDelay(pdMS_TO_TICKS(500));  // Increased delay to ensure task fully stops
    }
    
    // Record audio for voice command (after wake word)
    // Record for ~3-5 seconds or until silence detected
    const size_t record_duration_ms = 3000;  // 3 seconds
    const size_t sample_rate = 16000;
    const size_t record_samples = (record_duration_ms * sample_rate) / 1000;
    
    // Allocate buffer in PSRAM for audio recording
    int16_t *audio_buffer = (int16_t *)heap_caps_malloc(record_samples * sizeof(int16_t), 
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!audio_buffer) {
        ESP_LOGW(TAG, "PSRAM allocation failed, trying internal RAM");
        audio_buffer = (int16_t *)heap_caps_malloc(record_samples * sizeof(int16_t),
                                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!audio_buffer) {
            ESP_LOGE(TAG, "Failed to allocate audio buffer for voice command");
            return;
        }
    }
    
    ESP_LOGI(TAG, "🎤 Recording voice command after 'Hey Nap' (%zu samples, %.1f seconds)...", 
             record_samples, (float)record_duration_ms / 1000.0f);
    
    // Small delay to skip the wake word itself and start recording the command
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Record audio from microphone (after wake word)
    size_t samples_recorded = 0;
    const size_t chunk_size = 512;  // Read in chunks
    int64_t start_time = esp_timer_get_time();
    
    while (samples_recorded < record_samples) {
        size_t bytes_read = 0;
        size_t samples_to_read = (record_samples - samples_recorded > chunk_size) ? 
                                 chunk_size : (record_samples - samples_recorded);
        
        // Read 32-bit stereo samples and convert to 16-bit mono
        int32_t *temp_buffer = (int32_t *)malloc(samples_to_read * 2 * sizeof(int32_t));
        if (!temp_buffer) {
            ESP_LOGE(TAG, "Failed to allocate temp buffer for recording");
            break;
        }
        
        // Acquire mutex before reading from I2S channel
        esp_err_t ret = ESP_FAIL;
        if (xSemaphoreTake(s_i2s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // Read directly from I2S channel (bypassing esp_codec_dev_read bug)
            ret = i2s_channel_read(s_rx_handle, temp_buffer,
                                    samples_to_read * 2 * sizeof(int32_t),
                                    &bytes_read, 100);
            xSemaphoreGive(s_i2s_mutex);
        } else {
            ESP_LOGW(TAG, "Failed to acquire I2S mutex for recording");
        }
        
        if (ret == ESP_OK && bytes_read > 0) {
            // Convert 32-bit stereo to 16-bit mono
            size_t stereo_samples = bytes_read / sizeof(int32_t);
            for (size_t i = 0; i < stereo_samples / 2 && samples_recorded < record_samples; i++) {
                int32_t left = temp_buffer[i * 2];
                audio_buffer[samples_recorded++] = (int16_t)(left >> 16);  // Upper 16 bits
            }
        } else if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Microphone read error during recording: %s", esp_err_to_name(ret));
            free(temp_buffer);
            break;
        }
        free(temp_buffer);
        
        // Check timeout (safety)
        int64_t elapsed_ms = (esp_timer_get_time() - start_time) / 1000;
        if (elapsed_ms > record_duration_ms + 500) {
            ESP_LOGW(TAG, "Recording timeout");
            break;
        }
    }
    
    ESP_LOGI(TAG, "✅ Recorded %zu samples (%.2f seconds)", 
             samples_recorded, (float)samples_recorded / sample_rate);
    
    // Resume continuous mic capture if it was running
    // Use wake_word_manager_start() to restart properly
    if (was_running) {
        ESP_LOGI(TAG, "Resuming continuous mic capture...");
        // Small delay to ensure I2S channel is ready
        vTaskDelay(pdMS_TO_TICKS(100));
        // Restart using the existing function (it will check s_running and create task)
        s_running = false;  // Ensure it's false so start() will create the task
        esp_err_t ret = wake_word_manager_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart mic capture: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Mic capture restarted successfully");
        }
    }
    
    if (samples_recorded > 0) {
        // Validate audio - check if it's all zeros (microphone not working)
        int32_t sum = 0;
        int32_t sum_sq = 0;
        int16_t max_val = 0;
        int16_t min_val = 0;
        for (size_t i = 0; i < samples_recorded; i++) {
            int16_t sample = audio_buffer[i];
            sum += sample;
            sum_sq += (int32_t)sample * sample;
            if (sample > max_val) max_val = sample;
            if (sample < min_val) min_val = sample;
        }
        float rms = sqrtf((float)sum_sq / samples_recorded);
        float avg = (float)sum / samples_recorded;
        ESP_LOGI(TAG, "📊 Recorded audio stats: RMS=%.1f, avg=%.1f, peak=[%d, %d]", rms, avg, min_val, max_val);
        
        if (rms < 10.0f) {
            ESP_LOGW(TAG, "⚠️  Recorded audio appears to be silence (RMS=%.1f < 10) - microphone may not be working", rms);
            ESP_LOGW(TAG, "⚠️  STT will likely fail or return empty transcript");
        }
        
        // Process voice command: STT (Google Speech-to-Text) -> LLM (Gemini) -> TTS
        ESP_LOGI(TAG, "📤 Sending audio to Google Speech-to-Text API for transcription...");
        esp_err_t cmd_ret = voice_assistant_process_command(audio_buffer, samples_recorded);
        if (cmd_ret != ESP_OK) {
            ESP_LOGW(TAG, "Voice command processing failed: %s", esp_err_to_name(cmd_ret));
        } else {
            ESP_LOGI(TAG, "✅ Voice command processed successfully");
        }
    } else {
        ESP_LOGW(TAG, "No audio recorded, skipping STT");
    }
    
    free(audio_buffer);
}

// Microphone audio capture task
// ES7210 outputs 32-bit stereo samples, we convert to 16-bit mono for OpenWakeWord
static void mic_capture_task(void *pvParameters)
{
    const size_t buffer_size = 512; // 32ms at 16kHz (mono output)
    const size_t stereo_buffer_size = buffer_size * 2; // ES7210 outputs stereo
    
    // Buffer for 32-bit stereo samples from ES7210
    int32_t *stereo_buffer = (int32_t *)malloc(stereo_buffer_size * sizeof(int32_t));
    // Buffer for 16-bit mono samples for OpenWakeWord
    int16_t *mono_buffer = (int16_t *)malloc(buffer_size * sizeof(int16_t));
    
    if (!stereo_buffer || !mono_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffers");
        if (stereo_buffer) free(stereo_buffer);
        if (mono_buffer) free(mono_buffer);
        s_running = false;  // Ensure flag is cleared on error
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Microphone capture task started (32-bit stereo -> 16-bit mono conversion)");
    
    // Small delay to ensure I2S channel is ready
    vTaskDelay(pdMS_TO_TICKS(50));
    
    static size_t total_samples_captured = 0;
    static int chunk_count = 0;
    
    while (s_running) {
        // Check s_running at the start of each iteration
        if (!s_running) {
            break;
        }
        
        size_t bytes_read = 0;
        esp_err_t ret = ESP_FAIL;
        
        // Acquire mutex before reading from I2S channel
        if (xSemaphoreTake(s_i2s_mutex, portMAX_DELAY) == pdTRUE) {
            // Read directly from I2S channel (bypassing esp_codec_dev_read bug)
            // esp_codec_dev_read has a bug where it returns ESP_CODEC_DEV_OK (0) instead of bytes_read
            // Direct I2S read works correctly
            ret = i2s_channel_read(s_rx_handle, stereo_buffer, 
                                   stereo_buffer_size * sizeof(int32_t), 
                                   &bytes_read, 100);
            xSemaphoreGive(s_i2s_mutex);
        }
        
        // Debug: Log what direct I2S read returns (first 10 calls and every 100 calls)
        if (chunk_count < 10 || chunk_count % 100 == 0) {
            ESP_LOGI(TAG, "🔍 Direct I2S read: ret=%s, bytes_read=%zu (chunk #%d)", 
                     esp_err_to_name(ret), bytes_read, chunk_count);
        }
        
        if (ret == ESP_OK && bytes_read > 0) {
            // Convert 32-bit stereo to 16-bit mono
            // ES7210 outputs: [L0, R0, L1, R1, L2, R2, ...] via esp_codec_dev
            size_t stereo_samples = bytes_read / sizeof(int32_t);
            size_t mono_samples = stereo_samples / 2; // Each stereo pair becomes one mono sample
            
            // Log first few raw 32-bit samples for debugging (first chunk and every 100 chunks)
            if (chunk_count == 0 || chunk_count % 100 == 0) {
                ESP_LOGI(TAG, "Raw 32-bit stereo samples (first 8 of chunk #%d):", chunk_count);
                for (size_t i = 0; i < (stereo_samples < 8 ? stereo_samples : 8); i++) {
                    int32_t val = stereo_buffer[i];
                    int16_t upper = (int16_t)(val >> 16);
                    int16_t lower = (int16_t)(val & 0xFFFF);
                    ESP_LOGI(TAG, "  [%zu] 0x%08x (%d) [upper=0x%04x (%d), lower=0x%04x (%d)]", 
                             i, (unsigned int)val, (int)val, 
                             (unsigned short)upper, upper, (unsigned short)lower, lower);
                }
            }
            
            // Extract left channel and convert to 16-bit
            // Try both bit positions: upper 16 bits (>> 16) and lower 16 bits (no shift)
            int16_t max_val = 0;
            int16_t min_val = 0;
            int64_t sum = 0;
            for (size_t i = 0; i < mono_samples && i < buffer_size; i++) {
                // Take left channel (even indices)
                int32_t left_sample = stereo_buffer[i * 2];
                
                // Extract 16-bit from 32-bit I2S sample
                // ES7210 outputs 16-bit audio in the lower 16 bits of 32-bit I2S words
                // Upper 16 bits are typically sign extension or padding
                int16_t mono_sample_upper = (int16_t)(left_sample >> 16);
                int16_t mono_sample_lower = (int16_t)(left_sample & 0xFFFF);
                
                // Try lower 16 bits first (ES7210 typically uses lower bits for 16-bit audio)
                // If lower bits are very small (< 100) and upper bits have larger values, use upper
                int16_t mono_sample = mono_sample_lower;
                if (abs(mono_sample_lower) < 100 && abs(mono_sample_upper) > abs(mono_sample_lower)) {
                    mono_sample = mono_sample_upper;
                }
                
                mono_buffer[i] = mono_sample;
                
                if (mono_sample > max_val) max_val = mono_sample;
                if (mono_sample < min_val) min_val = mono_sample;
                sum += (int64_t)mono_sample;
            }
            int16_t avg = (int16_t)(sum / mono_samples);
            
            total_samples_captured += mono_samples;
            chunk_count++;
            
            // Log every 50 chunks (~1.6 seconds) or on first chunk
            if (chunk_count == 1 || chunk_count % 50 == 0) {
                ESP_LOGI(TAG, "🎤 Mic chunk #%d: %zu mono samples (from %zu stereo), peak=[%d, %d], avg=%d, total=%.1fs",
                         chunk_count, mono_samples, stereo_samples, min_val, max_val, avg,
                         (float)total_samples_captured / 16000.0f);
            } else {
                ESP_LOGD(TAG, "🎤 Mic chunk #%d: %zu samples, peak=[%d, %d]",
                         chunk_count, mono_samples, min_val, max_val);
            }
            
            // Process audio through OpenWakeWord (expects 16-bit mono)
            // Only process if still running (check again after conversion)
            if (s_running) {
                openwakeword_process(mono_buffer, mono_samples);
                
                // Also send to STT queue for parallel processing (non-blocking)
                if (s_stt_enabled && s_stt_audio_queue) {
                    // Allocate a fixed-size chunk for the queue (STT_CHUNK_SIZE = 512 samples)
                    int16_t *chunk_copy = (int16_t *)malloc(STT_CHUNK_SIZE * sizeof(int16_t));
                    if (chunk_copy) {
                        // Copy available samples and zero-pad if needed
                        size_t samples_to_copy = (mono_samples < STT_CHUNK_SIZE) ? mono_samples : STT_CHUNK_SIZE;
                        memcpy(chunk_copy, mono_buffer, samples_to_copy * sizeof(int16_t));
                        // Zero-pad if we have fewer samples than STT_CHUNK_SIZE
                        if (samples_to_copy < STT_CHUNK_SIZE) {
                            memset(chunk_copy + samples_to_copy, 0, (STT_CHUNK_SIZE - samples_to_copy) * sizeof(int16_t));
                        }
                        // Send pointer value through queue (queue item size is sizeof(int16_t *))
                        if (xQueueSend(s_stt_audio_queue, &chunk_copy, 0) != pdTRUE) {
                            // Queue full, drop this chunk
                            free(chunk_copy);
                            ESP_LOGD(TAG, "STT queue full, dropping chunk");
                        }
                    }
                }
            }
        } else if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Microphone read error: %s", esp_err_to_name(ret));
        } else {
            // bytes_read == 0 is normal (no data available) - log occasionally
            if (chunk_count % 100 == 0) {
                ESP_LOGD(TAG, "Microphone read returned 0 bytes (normal when no audio)");
            }
        }
    }
    
    // Cleanup
    free(stereo_buffer);
    free(mono_buffer);
    s_running = false;  // Ensure flag is cleared when task exits
    ESP_LOGI(TAG, "Microphone capture task stopped");
    vTaskDelete(NULL);
}

// STT processing task - accumulates audio chunks and sends to Google STT periodically
static void stt_processing_task(void *pvParameters)
{
    ESP_LOGI(TAG, "STT processing task started");
    
    // Allocate buffer for accumulating audio (2 seconds = 32000 samples at 16kHz)
    int16_t *stt_buffer = (int16_t *)heap_caps_malloc(STT_BUFFER_SAMPLES * sizeof(int16_t),
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!stt_buffer) {
        ESP_LOGW(TAG, "PSRAM allocation failed for STT buffer, trying internal RAM");
        stt_buffer = (int16_t *)heap_caps_malloc(STT_BUFFER_SAMPLES * sizeof(int16_t),
                                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!stt_buffer) {
            ESP_LOGE(TAG, "Failed to allocate STT buffer");
            vTaskDelete(NULL);
            return;
        }
    }
    
    size_t buffer_samples = 0;
    int64_t last_stt_time = esp_timer_get_time();
    int chunk_count = 0;
    
    while (s_stt_enabled && s_stt_audio_queue) {
        int16_t *chunk_ptr = NULL;
        
        // Wait for audio chunk pointer from queue (with timeout)
        if (xQueueReceive(s_stt_audio_queue, &chunk_ptr, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (!chunk_ptr) {
                continue;
            }
            
            chunk_count++;
            
            // Copy chunk to buffer (always STT_CHUNK_SIZE samples)
            size_t samples_to_copy = STT_CHUNK_SIZE;
            if (buffer_samples + samples_to_copy > STT_BUFFER_SAMPLES) {
                // Buffer full, send what we have and reset
                if (buffer_samples > 0) {
                    ESP_LOGI(TAG, "📤 [STT] Sending accumulated audio: %zu samples (~%.1fs)", 
                             buffer_samples, (float)buffer_samples / 16000.0f);
                    
                    char text[512] = {0};
                    esp_err_t stt_ret = gemini_stt(stt_buffer, buffer_samples, text, sizeof(text));
                    if (stt_ret == ESP_OK && strlen(text) > 0) {
                        ESP_LOGI(TAG, "✅ [STT] Transcribed: \"%s\"", text);
                        // TODO: Process transcribed text (e.g., send to LLM, execute commands)
                    } else if (stt_ret == ESP_OK) {
                        ESP_LOGD(TAG, "[STT] No transcription (silence or unrecognized)");
                    } else {
                        ESP_LOGW(TAG, "[STT] Transcription failed: %s", esp_err_to_name(stt_ret));
                    }
                }
                buffer_samples = 0;
            }
            
            // Copy chunk to buffer (chunk_ptr points to STT_CHUNK_SIZE samples)
            if (chunk_ptr) {
                memcpy(stt_buffer + buffer_samples, chunk_ptr, samples_to_copy * sizeof(int16_t));
                buffer_samples += samples_to_copy;
                free(chunk_ptr);
            }
            
            // Check if it's time to send to STT (every 2 seconds)
            int64_t now = esp_timer_get_time();
            int64_t elapsed_ms = (now - last_stt_time) / 1000;
            
            if (elapsed_ms >= STT_BUFFER_DURATION_MS && buffer_samples > 0) {
                ESP_LOGI(TAG, "📤 [STT] Sending audio to Google STT: %zu samples (~%.1fs, %d chunks)", 
                         buffer_samples, (float)buffer_samples / 16000.0f, chunk_count);
                
                char text[512] = {0};
                esp_err_t stt_ret = gemini_stt(stt_buffer, buffer_samples, text, sizeof(text));
                if (stt_ret == ESP_OK && strlen(text) > 0) {
                    ESP_LOGI(TAG, "✅ [STT] Transcribed: \"%s\"", text);
                    // TODO: Process transcribed text (e.g., send to LLM, execute commands)
                } else if (stt_ret == ESP_OK) {
                    ESP_LOGD(TAG, "[STT] No transcription (silence or unrecognized)");
                } else {
                    ESP_LOGW(TAG, "[STT] Transcription failed: %s", esp_err_to_name(stt_ret));
                }
                
                buffer_samples = 0;
                chunk_count = 0;
                last_stt_time = now;
            }
        }
    }
    
    // Cleanup
    free(stt_buffer);
    ESP_LOGI(TAG, "STT processing task stopped");
    vTaskDelete(NULL);
}

esp_err_t wake_word_manager_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Wake word manager already initialized");
        return ESP_OK;
    }
    
    // Initialize OpenWakeWord
    // OpenWakeWord typically uses 16kHz mono audio
    esp_err_t ret = openwakeword_init(16000, on_wake_word_detected);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize OpenWakeWord: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create mutex to protect I2S channel access
    s_i2s_mutex = xSemaphoreCreateMutex();
    if (!s_i2s_mutex) {
        ESP_LOGE(TAG, "Failed to create I2S mutex");
        openwakeword_deinit();
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize ES7210 using esp_codec_dev high-level API
    // This replaces the low-level register writes with the official API
    ESP_LOGI(TAG, "Initializing ES7210 using esp_codec_dev API...");
    ret = es7210_init_with_codec_dev();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ES7210: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_i2s_mutex);
        s_i2s_mutex = NULL;
        openwakeword_deinit();
        return ret;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "Wake word manager initialized");
    return ESP_OK;
}

esp_err_t wake_word_manager_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_running) {
        return ESP_OK;
    }
    
    // ES7210 codec device is already initialized and opened
    if (!s_es7210_dev) {
        ESP_LOGE(TAG, "ES7210 codec device not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "ES7210 codec device ready");
    
    // Start OpenWakeWord
    esp_err_t ret = openwakeword_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start OpenWakeWord: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize STT queue and task for parallel processing
    if (s_stt_enabled && !s_stt_audio_queue) {
        s_stt_audio_queue = xQueueCreate(STT_QUEUE_SIZE, sizeof(int16_t *));
        if (!s_stt_audio_queue) {
            ESP_LOGE(TAG, "Failed to create STT audio queue");
            // Continue without STT - not critical
        } else {
            ESP_LOGI(TAG, "STT audio queue created (size: %d chunks)", STT_QUEUE_SIZE);
            
            // Create STT processing task - pin to CPU 1 for audio processing
            xTaskCreatePinnedToCore(
                stt_processing_task,
                "stt_processor",
                8192,  // Larger stack for STT processing
                NULL,
                4,  // Lower priority than mic capture
                &s_stt_task_handle,
                1  // CPU 1 for audio processing
            );
            
            if (!s_stt_task_handle) {
                ESP_LOGE(TAG, "Failed to create STT processing task");
                vQueueDelete(s_stt_audio_queue);
                s_stt_audio_queue = NULL;
            } else {
                ESP_LOGI(TAG, "✅ Parallel STT processing started (sends every %dms)", STT_BUFFER_DURATION_MS);
            }
        }
    }
    
    // Create microphone capture task - pin to CPU 1 for audio processing
    s_running = true;
    TaskHandle_t task_handle;
    xTaskCreatePinnedToCore(
        mic_capture_task,
        "mic_capture",
        4096,
        NULL,
        5,
        &task_handle,
        1  // CPU 1 for audio processing
    );
    
    if (!task_handle) {
        ESP_LOGE(TAG, "Failed to create microphone capture task");
        openwakeword_stop();
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Wake word detection started");
    return ESP_OK;
}

void wake_word_manager_stop(void)
{
    if (!s_running) {
        return;
    }
    
    s_running = false;
    
    // Stop OpenWakeWord
    openwakeword_stop();
    
    // Stop STT processing
    if (s_stt_enabled) {
        s_stt_enabled = false;
        // Wait for STT task to exit
        if (s_stt_task_handle) {
            vTaskDelay(pdMS_TO_TICKS(500));
            s_stt_task_handle = NULL;
        }
        // Clear any remaining chunks in queue
        if (s_stt_audio_queue) {
            int16_t *chunk = NULL;
            while (xQueueReceive(s_stt_audio_queue, &chunk, 0) == pdTRUE) {
                if (chunk) {
                    free(chunk);
                }
            }
        }
    }
    
    // Task will exit on its own when s_running becomes false
    vTaskDelay(pdMS_TO_TICKS(200));
    
    ESP_LOGI(TAG, "Wake word detection stopped");
}

bool wake_word_manager_is_active(void)
{
    return s_running;
}

void wake_word_manager_pause(void)
{
    if (s_running) {
        ESP_LOGI(TAG, "Pausing wake word detection (e.g., during audio playback)");
        s_running = false;
        // Give the mic capture task time to exit
        vTaskDelay(pdMS_TO_TICKS(200));
        // Also pause OpenWakeWord processing
        openwakeword_stop();
        // Note: I2S channel remains enabled but task is stopped
        // Disabling I2S channel causes issues with TLS operations, so we just stop the task
    }
}

void wake_word_manager_resume(void)
{
    if (!s_running && s_initialized) {
        ESP_LOGI(TAG, "Resuming wake word detection");
        // Restart using the existing start function (I2S channel should still be enabled)
        esp_err_t ret = wake_word_manager_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to resume wake word detection: %s", esp_err_to_name(ret));
        }
    }
}

void wake_word_manager_deinit(void)
{
    wake_word_manager_stop();
    
    // Clean up STT queue
    if (s_stt_audio_queue) {
        vQueueDelete(s_stt_audio_queue);
        s_stt_audio_queue = NULL;
    }
    
    if (s_initialized) {
        // Close and delete ES7210 codec device
        if (s_es7210_dev) {
            esp_codec_dev_close(s_es7210_dev);
            esp_codec_dev_delete(s_es7210_dev);
            s_es7210_dev = NULL;
        }
        
        // Delete codec interfaces
        if (s_record_codec_if) {
            audio_codec_delete_codec_if(s_record_codec_if);
            s_record_codec_if = NULL;
        }
        if (s_record_ctrl_if) {
            audio_codec_delete_ctrl_if(s_record_ctrl_if);
            s_record_ctrl_if = NULL;
        }
        if (s_record_data_if) {
            audio_codec_delete_data_if(s_record_data_if);
            s_record_data_if = NULL;
        }
        
        // Disable and delete I2S channel
        if (s_rx_handle) {
            i2s_channel_disable(s_rx_handle);
            i2s_del_channel(s_rx_handle);
            s_rx_handle = NULL;
        }
        
        openwakeword_deinit();
        s_initialized = false;
    }
    
    ESP_LOGI(TAG, "Wake word manager deinitialized");
}
