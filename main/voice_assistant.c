#include "voice_assistant.h"
#include "gemini_api.h"
#include "wake_word_manager.h"
#include "audio_player.h"
#include "action_manager.h"
#include "wake_word_manager.h"  // For pause/resume during playback
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>
#include "cJSON.h"

static const char *TAG = "voice_assistant";

static voice_assistant_config_t s_config = {0};
static bool s_initialized = false;
static bool s_active = false;
static TaskHandle_t s_assistant_task = NULL;
static QueueHandle_t s_command_queue = NULL;

// Voice command processing task
static void assistant_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Voice assistant task started");
    
    while (s_active) {
        // Wait for wake word detection or manual command
        // For now, we'll process commands from the queue
        // In a full implementation, this would be triggered by wake word detection
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // TODO: Get audio from wake word detection or queue
        // This is a placeholder - actual implementation would:
        // 1. Wait for wake word detection
        // 2. Record audio after wake word
        // 3. Process through STT -> LLM -> TTS -> Playback
    }
    
    ESP_LOGI(TAG, "Voice assistant task stopped");
    vTaskDelete(NULL);
}

// Wake word callback - triggered when wake word is detected
__attribute__((unused)) static void on_wake_word_detected(const char *wake_word)
{
    ESP_LOGI(TAG, "Wake word '%s' detected - starting voice command capture", wake_word);
    
    // TODO: Start recording audio for command
    // For now, this is a placeholder
    // In full implementation:
    // 1. Start recording from microphone
    // 2. Record for ~5 seconds or until silence detected
    // 3. Send to voice_assistant_process_command()
}

// Function definitions for Gemini (matching SomnusDevice actions)
static const char *get_function_definitions_json(void)
{
    return 
    "{"
    "\"functionDeclarations\": ["
        "{"
            "\"name\": \"set_led_color\","
            "\"description\": \"Set LED strip to a solid color. Use this to change the LED color based on user requests.\","
            "\"parameters\": {"
                "\"type\": \"object\","
                "\"properties\": {"
                    "\"red\": {\"type\": \"integer\", \"description\": \"Red component (0-255)\"},"
                    "\"green\": {\"type\": \"integer\", \"description\": \"Green component (0-255)\"},"
                    "\"blue\": {\"type\": \"integer\", \"description\": \"Blue component (0-255)\"}"
                "},"
                "\"required\": [\"red\", \"green\", \"blue\"]"
            "}"
        "},"
        "{"
            "\"name\": \"set_led_pattern\","
            "\"description\": \"Set LED strip to a pattern like rainbow or clear. Use for special effects.\","
            "\"parameters\": {"
                "\"type\": \"object\","
                "\"properties\": {"
                    "\"pattern\": {\"type\": \"string\", \"enum\": [\"rainbow\", \"clear\"], \"description\": \"Pattern name\"}"
                "},"
                "\"required\": [\"pattern\"]"
            "}"
        "},"
        "{"
            "\"name\": \"set_led_intensity\","
            "\"description\": \"Set LED brightness/intensity (0.0 to 1.0). Use when user asks to dim or brighten LEDs.\","
            "\"parameters\": {"
                "\"type\": \"object\","
                "\"properties\": {"
                    "\"intensity\": {\"type\": \"number\", \"description\": \"Intensity from 0.0 (off) to 1.0 (max)\"}"
                "},"
                "\"required\": [\"intensity\"]"
            "}"
        "},"
        "{"
            "\"name\": \"set_volume\","
            "\"description\": \"Set audio volume (0.0 to 1.0). Use when user asks to change volume.\","
            "\"parameters\": {"
                "\"type\": \"object\","
                "\"properties\": {"
                    "\"volume\": {\"type\": \"number\", \"description\": \"Volume from 0.0 (mute) to 1.0 (max)\"}"
                "},"
                "\"required\": [\"volume\"]"
            "}"
        "},"
        "{"
            "\"name\": \"pause_device\","
            "\"description\": \"Pause the device (stop audio and clear LEDs). Use when user asks to pause or stop.\","
            "\"parameters\": {"
                "\"type\": \"object\","
                "\"properties\": {},"
                "\"required\": []"
            "}"
        "},"
        "{"
            "\"name\": \"resume_device\","
            "\"description\": \"Resume the device (restore audio and LEDs). Use when user asks to play or resume.\","
            "\"parameters\": {"
                "\"type\": \"object\","
                "\"properties\": {},"
                "\"required\": []"
            "}"
        "}"
    "]"
    "}";
}

// Convert Gemini function call to action_manager action
static esp_err_t execute_function_call(const gemini_function_call_t *func_call)
{
    if (!func_call || !func_call->is_function_call) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🔧 Executing function call: %s with args: %s", 
             func_call->function_name, func_call->arguments);
    
    // Parse function arguments
    cJSON *args = cJSON_Parse(func_call->arguments);
    if (!args) {
        ESP_LOGE(TAG, "Failed to parse function arguments");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_OK;
    
    // Map function names to actions
    if (strcmp(func_call->function_name, "set_led_color") == 0) {
        cJSON *r = cJSON_GetObjectItem(args, "red");
        cJSON *g = cJSON_GetObjectItem(args, "green");
        cJSON *b = cJSON_GetObjectItem(args, "blue");
        
        if (r && g && b && cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
            char pattern_json[256];
            snprintf(pattern_json, sizeof(pattern_json),
                    "{\"color\": [%d, %d, %d]}",
                    r->valueint, g->valueint, b->valueint);
            
            action_t action = {
                .type = ACTION_LED,
            };
            strncpy(action.data.led.pattern_data, pattern_json, sizeof(action.data.led.pattern_data) - 1);
            ret = action_manager_execute(&action);
        }
    } else if (strcmp(func_call->function_name, "set_led_pattern") == 0) {
        cJSON *pattern = cJSON_GetObjectItem(args, "pattern");
        if (pattern && cJSON_IsString(pattern)) {
            char pattern_json[256];
            snprintf(pattern_json, sizeof(pattern_json),
                    "{\"pattern\": \"%s\"}", pattern->valuestring);
            
            action_t action = {
                .type = ACTION_LED,
            };
            strncpy(action.data.led.pattern_data, pattern_json, sizeof(action.data.led.pattern_data) - 1);
            ret = action_manager_execute(&action);
        }
    } else if (strcmp(func_call->function_name, "set_led_intensity") == 0) {
        cJSON *intensity = cJSON_GetObjectItem(args, "intensity");
        if (intensity && cJSON_IsNumber(intensity)) {
            action_t action = {
                .type = ACTION_SET_LED_INTENSITY,
                .data.led_intensity.intensity = (float)intensity->valuedouble
            };
            ret = action_manager_execute(&action);
        }
    } else if (strcmp(func_call->function_name, "set_volume") == 0) {
        cJSON *volume = cJSON_GetObjectItem(args, "volume");
        if (volume && cJSON_IsNumber(volume)) {
            action_t action = {
                .type = ACTION_SET_VOLUME,
                .data.volume.volume = (float)volume->valuedouble
            };
            ret = action_manager_execute(&action);
        }
    } else if (strcmp(func_call->function_name, "pause_device") == 0) {
        action_t action = { .type = ACTION_PAUSE };
        ret = action_manager_execute(&action);
    } else if (strcmp(func_call->function_name, "resume_device") == 0) {
        action_t action = { .type = ACTION_PLAY };
        ret = action_manager_execute(&action);
    } else {
        ESP_LOGW(TAG, "Unknown function: %s", func_call->function_name);
        ret = ESP_ERR_NOT_FOUND;
    }
    
    cJSON_Delete(args);
    return ret;
}

// Process complete voice command: STT -> LLM (with function calling) -> Execute actions -> TTS -> Playback
static esp_err_t process_voice_command(const int16_t *audio_data, size_t audio_len)
{
    ESP_LOGI(TAG, "Processing voice command (%zu samples)", audio_len);
    
    // Step 1: Speech-to-Text
    char transcribed_text[512];
    esp_err_t ret = gemini_stt(audio_data, audio_len, transcribed_text, sizeof(transcribed_text));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "STT failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Transcribed: %s", transcribed_text);
    
    // Step 2: LLM with function calling - Get response from Gemini
    char llm_response[2048] = {0};
    gemini_function_call_t function_call = {0};
    const char *tools_json = get_function_definitions_json();
    
    ret = gemini_llm_with_functions(transcribed_text, tools_json, 
                                     llm_response, sizeof(llm_response),
                                     &function_call);
    
    // Check if LLM wants to call a function
    if (ret == ESP_ERR_NOT_FOUND && function_call.is_function_call) {
        ESP_LOGI(TAG, "LLM requested function call: %s", function_call.function_name);
        
        // Execute the function call
        esp_err_t action_ret = execute_function_call(&function_call);
        if (action_ret == ESP_OK) {
            // Function executed successfully, get confirmation text
            char confirm_prompt[1024];
            snprintf(confirm_prompt, sizeof(confirm_prompt),
                    "The user said: \"%s\". I executed the function %s. Provide a brief confirmation message (1-2 sentences).",
                    transcribed_text, function_call.function_name);
            
            // Get text response for confirmation
            ret = gemini_llm(confirm_prompt, llm_response, sizeof(llm_response));
            if (ret != ESP_OK) {
                // Fallback message
                snprintf(llm_response, sizeof(llm_response), "Done.");
            }
        } else {
            // Function execution failed
            snprintf(llm_response, sizeof(llm_response), 
                    "I tried to %s but encountered an error.", function_call.function_name);
        }
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LLM failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "LLM response: %s", llm_response);
    
    // Step 3: Text-to-Speech
    const size_t tts_buffer_size = 48000; // ~2 seconds at 24kHz
    int16_t *tts_audio = (int16_t *)malloc(tts_buffer_size * sizeof(int16_t));
    if (!tts_audio) {
        ESP_LOGE(TAG, "Failed to allocate TTS buffer");
        return ESP_ERR_NO_MEM;
    }
    
    size_t samples_written = 0;
    ret = gemini_tts(llm_response, tts_audio, tts_buffer_size, &samples_written);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TTS failed: %s", esp_err_to_name(ret));
        free(tts_audio);
        return ret;
    }
    
    ESP_LOGI(TAG, "TTS generated %zu samples", samples_written);
    
    // Step 4: Play audio response
    // Note: TTS typically outputs at 24kHz, but our audio player may be at 48kHz
    // We'll need to resample or configure TTS for 48kHz
    ret = audio_player_submit_pcm(tts_audio, samples_written, 24000, 1); // Mono, 24kHz
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Audio playback failed: %s", esp_err_to_name(ret));
    }
    
    free(tts_audio);
    return ESP_OK;
}

esp_err_t voice_assistant_init(const voice_assistant_config_t *config)
{
    if (!config || strlen(config->gemini_api_key) == 0) {
        ESP_LOGE(TAG, "Invalid voice assistant configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(&s_config, config, sizeof(voice_assistant_config_t));
    
    // Initialize action manager
    esp_err_t ret = action_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize action manager: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize Gemini API
    gemini_config_t gemini_cfg = {
        .api_key = {0},
        .model = {0}
    };
    strncpy(gemini_cfg.api_key, config->gemini_api_key, sizeof(gemini_cfg.api_key) - 1);
    strncpy(gemini_cfg.model, config->gemini_model, sizeof(gemini_cfg.model) - 1);
    
    if (strlen(gemini_cfg.model) == 0) {
        strncpy(gemini_cfg.model, "gemini-2.0-flash", sizeof(gemini_cfg.model) - 1);
    }
    
    ret = gemini_api_init(&gemini_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Gemini API: %s", esp_err_to_name(ret));
        action_manager_deinit();
        return ret;
    }
    
    // Create command queue
    s_command_queue = xQueueCreate(4, sizeof(size_t)); // Store audio buffer pointers
    if (!s_command_queue) {
        ESP_LOGE(TAG, "Failed to create command queue");
        gemini_api_deinit();
        action_manager_deinit();
        return ESP_ERR_NO_MEM;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "Voice assistant initialized with function calling support");
    return ESP_OK;
}

esp_err_t voice_assistant_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_active) {
        return ESP_OK;
    }
    
    s_active = true;
    
    // Create assistant task
    xTaskCreate(
        assistant_task,
        "voice_assistant",
        8192,
        NULL,
        5,
        &s_assistant_task
    );
    
    if (!s_assistant_task) {
        ESP_LOGE(TAG, "Failed to create assistant task");
        s_active = false;
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Voice assistant started");
    return ESP_OK;
}

void voice_assistant_stop(void)
{
    if (!s_active) {
        return;
    }
    
    s_active = false;
    
    if (s_assistant_task) {
        vTaskDelay(pdMS_TO_TICKS(200));
        vTaskDelete(s_assistant_task);
        s_assistant_task = NULL;
    }
    
    ESP_LOGI(TAG, "Voice assistant stopped");
}

esp_err_t voice_assistant_process_command(const int16_t *audio_data, size_t audio_len)
{
    if (!s_initialized || !s_active) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return process_voice_command(audio_data, audio_len);
}

bool voice_assistant_is_active(void)
{
    return s_active;
}

void voice_assistant_deinit(void)
{
    voice_assistant_stop();
    
    if (s_command_queue) {
        vQueueDelete(s_command_queue);
        s_command_queue = NULL;
    }
    
    gemini_api_deinit();
    action_manager_deinit();
    s_initialized = false;
    
    ESP_LOGI(TAG, "Voice assistant deinitialized");
}

// Streaming TTS playback callback
// Called with decoded PCM audio chunks as they arrive from the API
static esp_err_t tts_playback_callback(const int16_t *samples, size_t sample_count, void *user_data)
{
    // user_data is unused, but could be used to pass state if needed
    (void)user_data;

    if (!samples || sample_count == 0) {
        return ESP_OK;
    }

    // Submit PCM chunk directly to audio player (24kHz, mono)
    // This avoids buffering entire audio in memory
    // Note: Wake word detection should be paused during TTS playback to prevent feedback
    return audio_player_submit_pcm(samples, sample_count, 24000, 1);
}

// Test TTS function - generate and play audio from text using streaming
// Gracefully handles network errors (e.g., in China where Google is blocked)
esp_err_t voice_assistant_test_tts(const char *text)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Voice assistant not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "🎤 Testing TTS with text: \"%s\"", text);

    // Pause wake word detection during TTS playback to prevent feedback loop
    wake_word_manager_pause();

    // Check available memory for diagnostics
    size_t total_free = esp_get_free_heap_size();
    size_t largest_default = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    ESP_LOGI(TAG, "Memory before streaming TTS: Total free=%lu bytes, Largest block=%lu bytes",
             (unsigned long)total_free, (unsigned long)largest_default);

    // Use streaming TTS - audio chunks are decoded and played as they arrive
    // No need to allocate 80KB buffer for entire audio
    // Timeout is typically 30 seconds, but will fail faster if network is down
    esp_err_t ret = gemini_tts_streaming(text, tts_playback_callback, NULL);
    
    // Resume wake word detection after TTS completes
    wake_word_manager_resume();
    if (ret != ESP_OK) {
        // Graceful degradation: log warning but don't crash
        // Common reasons: no internet, firewall blocks Google (China), API quota exceeded
        ESP_LOGW(TAG, "⚠️  TTS streaming failed: %s", esp_err_to_name(ret));
        ESP_LOGI(TAG, "Continuing without audio - LED effects will continue");
        return ret;
    }

    ESP_LOGI(TAG, "✅ Streaming TTS completed successfully");

    // Log free memory after TTS
    size_t free_heap_after = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "Memory after streaming TTS: %lu bytes free", (unsigned long)free_heap_after);

    return ESP_OK;
}
