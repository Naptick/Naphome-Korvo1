#include "wake_word_manager.h"
#include "openwakeword_esp32.h"
#include "korvo1.h"
#include "voice_assistant.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include <string.h>
#include <math.h>

static const char *TAG = "wake_word_mgr";

// ES7210 ADC I2C address (7-bit)
#define ES7210_ADDR_7BIT 0x40

// ES7210 register definitions
#define ES7210_CHIP_ID_REG 0xFD
#define ES7210_RESET_REG 0x00
#define ES7210_CLOCK_ON_REG 0x08
#define ES7210_MASTER_CLK_REG 0x09
#define ES7210_ADC_MCLK_REG 0x0A
#define ES7210_ADC_FSCLK_REG 0x0B
#define ES7210_ADC_SAMPLE_REG 0x0C
#define ES7210_ADC_ANALOG_REG 0x0D
#define ES7210_ADC_DIGITAL_REG 0x0E
#define ES7210_ADC_DPF_REG 0x0F
#define ES7210_ADC_CTRL_REG 0x10

static korvo1_t s_mic = {0};
static i2c_port_t s_i2c_port = I2C_NUM_0;  // Use same I2C bus as ES8311
static bool s_initialized = false;
static bool s_running = false;

// ES7210 I2C helper functions
static esp_err_t es7210_write_reg(uint8_t reg, uint8_t value)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ES7210_ADDR_7BIT << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(s_i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ES7210 write failed reg=0x%02x val=0x%02x err=%s", reg, value, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t es7210_read_reg(uint8_t reg, uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ES7210_ADDR_7BIT << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ES7210_ADDR_7BIT << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, value, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(s_i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t es7210_init(void)
{
    ESP_LOGI(TAG, "Initializing ES7210 ADC via I2C...");
    
    // Verify I2C bus is available (should be initialized by audio_player)
    // Try a simple I2C operation to check if bus is ready
    i2c_cmd_handle_t test_cmd = i2c_cmd_link_create();
    i2c_master_start(test_cmd);
    i2c_master_write_byte(test_cmd, (ES7210_ADDR_7BIT << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(test_cmd);
    esp_err_t test_err = i2c_master_cmd_begin(s_i2c_port, test_cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(test_cmd);
    
    if (test_err != ESP_OK && test_err != ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "I2C bus may not be ready (audio_player should initialize it first): %s", esp_err_to_name(test_err));
        // Continue anyway - ES7210 might work without explicit I2C config
        return ESP_OK;
    }
    
    // Try to read chip ID to verify device is present
    uint8_t chip_id = 0;
    esp_err_t err = es7210_read_reg(ES7210_CHIP_ID_REG, &chip_id);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "ES7210 detected: Chip ID=0x%02x", chip_id);
    } else {
        ESP_LOGW(TAG, "ES7210 probe failed (may not be present or I2C issue): %s", esp_err_to_name(err));
        // Continue anyway - I2S might still work, or ES7210 might be auto-configured
        return ESP_OK;  // Don't fail initialization if probe fails
    }
    
    // ES7210 initialization sequence for 16kHz recording
    // Note: ES7210 may work with minimal configuration - I2S setup might be sufficient
    // Try basic initialization, but don't fail if it doesn't work
    
    // Reset sequence
    if (es7210_write_reg(ES7210_RESET_REG, 0xFF) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(10));
        es7210_write_reg(ES7210_RESET_REG, 0x00);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Minimal configuration - enable clocks and ADC
    // These writes may fail if ES7210 isn't responding, but that's OK - I2S might still work
    es7210_write_reg(ES7210_CLOCK_ON_REG, 0xFF);  // Enable all clocks
    es7210_write_reg(ES7210_ADC_DIGITAL_REG, 0x01);  // Enable ADC digital path
    es7210_write_reg(ES7210_ADC_CTRL_REG, 0x01);     // Enable ADC
    
    vTaskDelay(pdMS_TO_TICKS(50)); // Allow ADC to stabilize
    
    ESP_LOGI(TAG, "ES7210 ADC initialized for 16kHz mono recording");
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
        
        esp_err_t ret = korvo1_read(&s_mic, 
                                    audio_buffer + samples_recorded,
                                    samples_to_read * sizeof(int16_t),
                                    &bytes_read,
                                    pdMS_TO_TICKS(100));
        
        if (ret == ESP_OK && bytes_read > 0) {
            samples_recorded += bytes_read / sizeof(int16_t);
        } else if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Microphone read error during recording: %s", esp_err_to_name(ret));
            break;
        }
        
        // Check timeout (safety)
        int64_t elapsed_ms = (esp_timer_get_time() - start_time) / 1000;
        if (elapsed_ms > record_duration_ms + 500) {
            ESP_LOGW(TAG, "Recording timeout");
            break;
        }
    }
    
    ESP_LOGI(TAG, "✅ Recorded %zu samples (%.2f seconds)", 
             samples_recorded, (float)samples_recorded / sample_rate);
    
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
static void mic_capture_task(void *pvParameters)
{
    const size_t buffer_size = 512; // 32ms at 16kHz
    int16_t *audio_buffer = (int16_t *)malloc(buffer_size * sizeof(int16_t));
    
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Microphone capture task started");
    
    static size_t total_samples_captured = 0;
    static int chunk_count = 0;
    
    while (s_running) {
        size_t bytes_read = 0;
        esp_err_t ret = korvo1_read(&s_mic, audio_buffer, 
                                    buffer_size * sizeof(int16_t), 
                                    &bytes_read, 
                                    pdMS_TO_TICKS(100));
        
        if (ret == ESP_OK && bytes_read > 0) {
            size_t samples_read = bytes_read / sizeof(int16_t);
            total_samples_captured += samples_read;
            chunk_count++;
            
            // Calculate quick stats for debug
            int16_t max_val = audio_buffer[0];
            int16_t min_val = audio_buffer[0];
            int64_t sum = 0;
            for (size_t i = 0; i < samples_read; i++) {
                if (audio_buffer[i] > max_val) max_val = audio_buffer[i];
                if (audio_buffer[i] < min_val) min_val = audio_buffer[i];
                sum += (int64_t)audio_buffer[i];
            }
            int16_t avg = (int16_t)(sum / samples_read);
            
            // Log every 50 chunks (~1.6 seconds) or on first chunk
            if (chunk_count == 1 || chunk_count % 50 == 0) {
                ESP_LOGI(TAG, "🎤 Mic chunk #%d: %zu samples, peak=[%d, %d], avg=%d, total=%.1fs",
                         chunk_count, samples_read, min_val, max_val, avg,
                         (float)total_samples_captured / 16000.0f);
            } else {
                ESP_LOGD(TAG, "🎤 Mic chunk #%d: %zu samples, peak=[%d, %d]",
                         chunk_count, samples_read, min_val, max_val);
            }
            
            // Process audio through OpenWakeWord
            openwakeword_process(audio_buffer, samples_read);
        } else if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Microphone read error: %s", esp_err_to_name(ret));
        } else {
            // Timeout is normal - log occasionally
            if (chunk_count % 100 == 0) {
                ESP_LOGD(TAG, "Microphone read timeout (normal when no audio)");
            }
        }
    }
    
    free(audio_buffer);
    ESP_LOGI(TAG, "Microphone capture task stopped");
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
    
    // Initialize ES7210 ADC via I2C (must be done before I2S setup)
    // ES7210 needs I2C configuration to enable audio output
    ret = es7210_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ES7210 I2C init failed (continuing anyway): %s", esp_err_to_name(ret));
        // Continue - I2S might still work, or ES7210 might be auto-configured
    }
    
    // Initialize Korvo1 microphone
    // Korvo1 uses ES7210 ADC on I2S1 (standard I2S, not PDM)
    // GPIO pins per esp-skainet BSP: SCLK=GPIO10, LRCK=GPIO9, SDIN=GPIO11, MCLK=GPIO20
    korvo1_config_t mic_config = {
        .port = I2S_NUM_1,  // Use I2S1 for microphone (I2S0 is for speaker/ES8311)
        .din_io_num = GPIO_NUM_11,  // I2S data input (ES7210 SDIN)
        .bclk_io_num = GPIO_NUM_10, // I2S bit clock (ES7210 SCLK)
        .ws_io_num = GPIO_NUM_9,    // I2S word select (ES7210 LRCK)
        .mclk_io_num = GPIO_NUM_20, // Master clock (ES7210 MCLK)
        .sample_rate_hz = 16000,    // 16kHz for wake word detection
        .dma_buffer_count = 4,
        .dma_buffer_len = 256,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // Mono (use left channel from ES7210)
    };
    
    ret = korvo1_init(&s_mic, &mic_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize microphone: %s", esp_err_to_name(ret));
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
    
    // Start OpenWakeWord
    esp_err_t ret = openwakeword_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start OpenWakeWord: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Start microphone
    ret = korvo1_start(&s_mic);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start microphone: %s", esp_err_to_name(ret));
        openwakeword_stop();
        return ret;
    }
    
    // Create microphone capture task
    s_running = true;
    TaskHandle_t task_handle;
    xTaskCreate(
        mic_capture_task,
        "mic_capture",
        4096,
        NULL,
        5,
        &task_handle
    );
    
    if (!task_handle) {
        ESP_LOGE(TAG, "Failed to create microphone capture task");
        korvo1_stop(&s_mic);
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
    
    // Stop microphone
    korvo1_stop(&s_mic);
    
    // Stop OpenWakeWord
    openwakeword_stop();
    
    // Task will exit on its own when s_running becomes false
    vTaskDelay(pdMS_TO_TICKS(200));
    
    ESP_LOGI(TAG, "Wake word detection stopped");
}

bool wake_word_manager_is_active(void)
{
    return s_running;
}

void wake_word_manager_deinit(void)
{
    wake_word_manager_stop();
    
    if (s_initialized) {
        korvo1_deinit(&s_mic);
        openwakeword_deinit();
        s_initialized = false;
    }
    
    ESP_LOGI(TAG, "Wake word manager deinitialized");
}
