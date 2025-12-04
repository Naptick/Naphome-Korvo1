#include "gemini_api.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "streaming_base64.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static const char *TAG = "gemini_api";

static gemini_config_t s_config = {0};
static bool s_initialized = false;

// HTTP response buffer
typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} http_buffer_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buffer_t *buf = (http_buffer_t *)evt->user_data;
    
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len == 0) {
                break;
            }
            // Check if buffer would overflow
            if (buf->len + evt->data_len > buf->cap) {
                // Don't log in interrupt handler - it blocks the main task from watchdog
                // Pre-allocated buffer should be sufficient for Gemini API responses
                return ESP_ERR_NO_MEM;
            }
            // Copy data to buffer (safe, no realloc needed)
            memcpy(buf->data + buf->len, evt->data, evt->data_len);
            buf->len += evt->data_len;
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Build WAV file from PCM samples
static esp_err_t build_wav_from_pcm(const int16_t *pcm, size_t sample_count, int sample_rate_hz, uint8_t **out_buf, size_t *out_len)
{
    size_t data_bytes = sample_count * sizeof(int16_t);
    size_t total_bytes = 44 + data_bytes; // WAV header (44 bytes) + data
    
    uint8_t *wav = malloc(total_bytes);
    if (!wav) {
        return ESP_ERR_NO_MEM;
    }
    
    // WAV header
    uint8_t header[44] = {
        'R', 'I', 'F', 'F',
        (uint8_t)(total_bytes - 8), (uint8_t)((total_bytes - 8) >> 8), 
        (uint8_t)((total_bytes - 8) >> 16), (uint8_t)((total_bytes - 8) >> 24),
        'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ',
        16, 0, 0, 0,  // fmt chunk size
        1, 0,  // audio format (1 = PCM)
        1, 0,  // num channels (1 = mono)
        (uint8_t)(sample_rate_hz), (uint8_t)(sample_rate_hz >> 8),
        (uint8_t)(sample_rate_hz >> 16), (uint8_t)(sample_rate_hz >> 24),
        (uint8_t)(sample_rate_hz * 2), (uint8_t)((sample_rate_hz * 2) >> 8),
        (uint8_t)((sample_rate_hz * 2) >> 16), (uint8_t)((sample_rate_hz * 2) >> 24),
        2, 0,  // block align
        16, 0,  // bits per sample
        'd', 'a', 't', 'a',
        (uint8_t)(data_bytes), (uint8_t)(data_bytes >> 8),
        (uint8_t)(data_bytes >> 16), (uint8_t)(data_bytes >> 24)
    };
    
    memcpy(wav, header, sizeof(header));
    memcpy(wav + sizeof(header), pcm, data_bytes);
    
    *out_buf = wav;
    *out_len = total_bytes;
    return ESP_OK;
}

// Base64 encode with allocation
static esp_err_t base64_encode_alloc(const uint8_t *input, size_t input_len, char **out_str)
{
    if (!input || !out_str) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t target_len = ((input_len + 2) / 3) * 4 + 1;
    char *encoded = malloc(target_len);
    if (!encoded) {
        return ESP_ERR_NO_MEM;
    }
    size_t written = 0;
    int ret = mbedtls_base64_encode((unsigned char *)encoded, target_len, &written, input, input_len);
    if (ret != 0) {
        free(encoded);
        return ESP_FAIL;
    }
    encoded[written] = '\0';
    *out_str = encoded;
    return ESP_OK;
}

// HTTP POST request helper with proper response handling
static esp_err_t http_post_json_with_auth(const char *url, const char *json_data, const char *auth_header, http_buffer_t *response)
{
    // Determine buffer size based on API endpoint
    // TTS responses are base64-encoded (35-50KB raw response, ~26-37KB after base64 decode)
    // LLM responses are JSON text (~20-40KB)
    // Device has only ~250KB free heap, so be conservative
    size_t RESPONSE_BUFFER_SIZE = 128 * 1024;  // Default 128KB for LLM

    // Check if this is a TTS request (use large buffer)
    if (strstr(url, "texttospeech") != NULL) {
        RESPONSE_BUFFER_SIZE = 256 * 1024;  // TTS: 256KB (with PSRAM enabled, plenty of margin)
    }

    if (!response->data) {
        // Allocate fixed-size buffer from heap
        response->data = malloc(RESPONSE_BUFFER_SIZE);
        if (!response->data) {
            // If allocation fails, try smaller buffer (32KB)
            const size_t FALLBACK_SIZE = 32 * 1024;
            ESP_LOGW(TAG, "Primary allocation (%zu bytes) failed, trying fallback (32KB)", RESPONSE_BUFFER_SIZE);
            response->data = malloc(FALLBACK_SIZE);
            if (!response->data) {
                // Last resort: 24KB minimum buffer
                const size_t MINIMUM_SIZE = 24 * 1024;
                ESP_LOGW(TAG, "Fallback allocation (32KB) failed, trying minimum (24KB)");
                response->data = malloc(MINIMUM_SIZE);
                if (!response->data) {
                    ESP_LOGE(TAG, "Failed to allocate even 24KB response buffer");
                    return ESP_ERR_NO_MEM;
                }
                response->cap = MINIMUM_SIZE;
            } else {
                response->cap = FALLBACK_SIZE;
            }
        } else {
            response->cap = RESPONSE_BUFFER_SIZE;
        }
        response->len = 0;
        ESP_LOGD(TAG, "Allocated response buffer: %zu bytes", response->cap);
    }

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = response,
        .timeout_ms = 30000,
        .skip_cert_common_name_check = false,  // Enable certificate verification
        .crt_bundle_attach = esp_crt_bundle_attach,  // Use certificate bundle for TLS verification
        .is_async = false,
        .disable_auto_redirect = false,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }
    
    // Set headers
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    // Note: Don't set Accept-Encoding headers - let server decide
    // ESP32's HTTP client has automatic gzip decompression support
    if (auth_header) {
        esp_http_client_set_header(client, "Authorization", auth_header);
    }
    
    // Set POST data
    esp_http_client_set_post_field(client, json_data, strlen(json_data));
    
    // Perform request
    int64_t start_time = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    int64_t elapsed_us = esp_timer_get_time() - start_time;

    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP response: %d (took %lld ms), response buffer: len=%zu, data=%p",
             status_code, elapsed_us / 1000, response->len, response->data);

    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
        return err;
    }

    if (status_code / 100 != 2) {
        ESP_LOGE(TAG, "HTTP request failed with status %d", status_code);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t gemini_api_init(const gemini_config_t *config)
{
    if (!config || strlen(config->api_key) == 0) {
        ESP_LOGE(TAG, "Invalid Gemini API configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(&s_config, config, sizeof(gemini_config_t));
    
    // Default model if not specified
    if (strlen(s_config.model) == 0) {
        strncpy(s_config.model, "gemini-2.0-flash", sizeof(s_config.model) - 1);
    }
    
    // Initialize global CA store with certificate bundle
    // The certificate bundle is automatically used when use_global_ca_store = true
    // We just need to ensure it's available - esp_http_client will use it automatically
    #ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    ESP_LOGI(TAG, "Certificate bundle enabled - will be used for TLS verification");
    #endif
    
    s_initialized = true;
    ESP_LOGI(TAG, "Gemini API initialized (model: %s)", s_config.model);
    return ESP_OK;
}

esp_err_t gemini_stt(const int16_t *audio_data, size_t audio_len, char *text_out, size_t text_len)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!audio_data || !text_out || audio_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    float duration_sec = 16000 > 0 ? (float)audio_len / 16000.0f : 0.0f;
    ESP_LOGI(TAG, "🔊 [Gemini STT] Starting transcription: %zu samples @ 16000 Hz (%.2f sec)", 
             audio_len, duration_sec);
    
    // Build WAV file from PCM
    uint8_t *wav = NULL;
    size_t wav_len = 0;
    esp_err_t ret = build_wav_from_pcm(audio_data, audio_len, 16000, &wav, &wav_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build WAV: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Base64 encode WAV
    char *base64_audio = NULL;
    ret = base64_encode_alloc(wav, wav_len, &base64_audio);
    free(wav);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to base64 encode: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Build JSON request for Google Speech-to-Text API
    cJSON *root = cJSON_CreateObject();
    cJSON *config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "encoding", "LINEAR16");
    cJSON_AddNumberToObject(config, "sampleRateHertz", 16000);
    cJSON_AddStringToObject(config, "languageCode", "en-US");
    cJSON_AddItemToObject(root, "config", config);
    
    cJSON *audio = cJSON_CreateObject();
    cJSON_AddStringToObject(audio, "content", base64_audio);
    cJSON_AddItemToObject(root, "audio", audio);
    
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(base64_audio);
    if (!payload) {
        ESP_LOGE(TAG, "Failed to create JSON payload");
        return ESP_ERR_NO_MEM;
    }
    
    // Build URL with API key
    char url[512];
    snprintf(url, sizeof(url), "https://speech.googleapis.com/v1/speech:recognize?key=%s", s_config.api_key);
    
    // Perform HTTP request
    http_buffer_t response = {0};
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_config.api_key);
    
    ret = http_post_json_with_auth(url, payload, auth_header, &response);
    free(payload);
    
    if (ret != ESP_OK) {
        if (response.data) free(response.data);
        return ret;
    }
    
    // Parse response
    if (!response.data || response.len == 0) {
        ESP_LOGE(TAG, "Empty response");
        if (response.data) free(response.data);
        return ESP_FAIL;
    }
    
    response.data = realloc(response.data, response.len + 1);
    response.data[response.len] = '\0';
    
    cJSON *response_json = cJSON_Parse((char *)response.data);
    free(response.data);
    
    if (!response_json) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return ESP_FAIL;
    }
    
    // Extract transcript
    cJSON *results = cJSON_GetObjectItem(response_json, "results");
    if (results && cJSON_IsArray(results) && cJSON_GetArraySize(results) > 0) {
        cJSON *result = cJSON_GetArrayItem(results, 0);
        if (result) {
            cJSON *alternatives = cJSON_GetObjectItem(result, "alternatives");
            if (alternatives && cJSON_IsArray(alternatives) && cJSON_GetArraySize(alternatives) > 0) {
                cJSON *alt = cJSON_GetArrayItem(alternatives, 0);
                if (alt) {
                    cJSON *transcript = cJSON_GetObjectItem(alt, "transcript");
                    if (transcript && cJSON_IsString(transcript)) {
                        strncpy(text_out, transcript->valuestring, text_len - 1);
                        text_out[text_len - 1] = '\0';
                        cJSON_Delete(response_json);
                        ESP_LOGI(TAG, "✅ [Gemini STT] Success: \"%s\"", text_out);
                        return ESP_OK;
                    }
                }
            }
        }
    }
    
    cJSON_Delete(response_json);
    ESP_LOGE(TAG, "❌ [Gemini STT] Failed to extract transcript from response");
    return ESP_FAIL;
}

esp_err_t gemini_llm(const char *prompt, char *response, size_t response_len)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!prompt || !response) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "💬 [Gemini LLM] Generating response for: \"%.100s%s\"", 
             prompt, strlen(prompt) > 100 ? "..." : "");
    
    // Build JSON request for Gemini API
    cJSON *root = cJSON_CreateObject();
    cJSON *contents = cJSON_CreateArray();
    cJSON *content = cJSON_CreateObject();
    cJSON *parts = cJSON_CreateArray();
    cJSON *part = cJSON_CreateObject();
    
    cJSON_AddItemToObject(root, "contents", contents);
    cJSON_AddItemToArray(contents, content);
    cJSON_AddItemToObject(content, "parts", parts);
    cJSON_AddItemToArray(parts, part);
    cJSON_AddStringToObject(part, "text", prompt);
    
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        ESP_LOGE(TAG, "Failed to create JSON payload");
        return ESP_ERR_NO_MEM;
    }
    
    // Gemini API endpoint
    char url[512];
    snprintf(url, sizeof(url), 
             "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s",
             s_config.model, s_config.api_key);
    
    // Perform HTTP request
    http_buffer_t http_response = {0};
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "X-Goog-Api-Key: %s", s_config.api_key);
    
    esp_err_t ret = http_post_json_with_auth(url, payload, NULL, &http_response);
    free(payload);
    
    if (ret != ESP_OK) {
        if (http_response.data) free(http_response.data);
        return ret;
    }
    
    // Parse response
    if (!http_response.data || http_response.len == 0) {
        ESP_LOGE(TAG, "Empty response");
        if (http_response.data) free(http_response.data);
        return ESP_FAIL;
    }
    
    http_response.data = realloc(http_response.data, http_response.len + 1);
    http_response.data[http_response.len] = '\0';
    
    cJSON *response_json = cJSON_Parse((char *)http_response.data);
    free(http_response.data);
    
    if (!response_json) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return ESP_FAIL;
    }
    
    // Extract text from Gemini response
    cJSON *candidates = cJSON_GetObjectItem(response_json, "candidates");
    if (candidates && cJSON_IsArray(candidates) && cJSON_GetArraySize(candidates) > 0) {
        cJSON *candidate = cJSON_GetArrayItem(candidates, 0);
        if (candidate) {
            cJSON *content_obj = cJSON_GetObjectItem(candidate, "content");
            if (content_obj) {
                cJSON *parts = cJSON_GetObjectItem(content_obj, "parts");
                if (parts && cJSON_IsArray(parts) && cJSON_GetArraySize(parts) > 0) {
                    cJSON *part = cJSON_GetArrayItem(parts, 0);
                    if (part) {
                        cJSON *text = cJSON_GetObjectItem(part, "text");
                        if (text && cJSON_IsString(text)) {
                            strncpy(response, text->valuestring, response_len - 1);
                            response[response_len - 1] = '\0';
                            cJSON_Delete(response_json);
                            ESP_LOGI(TAG, "✅ [Gemini LLM] Success: \"%.200s%s\"", 
                                     response, strlen(response) > 200 ? "..." : "");
                            return ESP_OK;
                        }
                    }
                }
            }
        }
    }
    
    cJSON_Delete(response_json);
    ESP_LOGE(TAG, "❌ [Gemini LLM] Failed to extract text from response");
    return ESP_FAIL;
}

esp_err_t gemini_tts(const char *text, int16_t *audio_out, size_t audio_len, size_t *samples_written)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!text || !audio_out || !samples_written) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🔊 [Gemini TTS] Generating speech: \"%.100s%s\"", 
             text, strlen(text) > 100 ? "..." : "");
    
    // Build JSON request for Google Cloud Text-to-Speech API
    cJSON *root = cJSON_CreateObject();
    cJSON *input = cJSON_CreateObject();
    cJSON *voice = cJSON_CreateObject();
    cJSON *audioConfig = cJSON_CreateObject();
    
    cJSON_AddItemToObject(root, "input", input);
    cJSON_AddItemToObject(root, "voice", voice);
    cJSON_AddItemToObject(root, "audioConfig", audioConfig);
    
    cJSON_AddStringToObject(input, "text", text);
    cJSON_AddStringToObject(voice, "languageCode", "en-US");
    cJSON_AddStringToObject(voice, "name", "en-US-Neural2-D");
    cJSON_AddStringToObject(audioConfig, "audioEncoding", "LINEAR16");
    cJSON_AddNumberToObject(audioConfig, "sampleRateHertz", 24000);
    
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        ESP_LOGE(TAG, "Failed to create JSON payload");
        return ESP_ERR_NO_MEM;
    }
    
    char url[512];
    snprintf(url, sizeof(url), 
             "https://texttospeech.googleapis.com/v1/text:synthesize?key=%s",
             s_config.api_key);
    
    // Perform HTTP request
    http_buffer_t http_response = {0};

    // TTS uses query parameter authentication, no auth header needed
    esp_err_t ret = http_post_json_with_auth(url, payload, NULL, &http_response);
    free(payload);
    
    if (ret != ESP_OK) {
        if (http_response.data) free(http_response.data);
        return ret;
    }
    
    // Parse response
    if (!http_response.data || http_response.len == 0) {
        ESP_LOGE(TAG, "Empty response");
        if (http_response.data) free(http_response.data);
        return ESP_FAIL;
    }
    
    http_response.data = realloc(http_response.data, http_response.len + 1);
    http_response.data[http_response.len] = '\0';
    
    cJSON *response_json = cJSON_Parse((char *)http_response.data);
    free(http_response.data);
    
    if (!response_json) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return ESP_FAIL;
    }
    
    cJSON *audioContent = cJSON_GetObjectItem(response_json, "audioContent");
    if (audioContent && cJSON_IsString(audioContent)) {
        // Decode base64 audio
        const char *base64_audio = audioContent->valuestring;
        size_t base64_len = strlen(base64_audio);
        size_t decoded_len = 0;
        
        // Calculate required buffer size
        size_t max_decoded = (base64_len / 4) * 3;
        if (max_decoded > audio_len * sizeof(int16_t)) {
            max_decoded = audio_len * sizeof(int16_t);
        }
        
        int mbedtls_ret = mbedtls_base64_decode((unsigned char *)audio_out, 
                                                max_decoded,
                                                &decoded_len,
                                                (const unsigned char *)base64_audio,
                                                base64_len);
        
        cJSON_Delete(response_json);
        
        if (mbedtls_ret != 0) {
            ESP_LOGE(TAG, "❌ [Gemini TTS] Base64 decode failed: %d", mbedtls_ret);
            return ESP_FAIL;
        }
        
        *samples_written = decoded_len / sizeof(int16_t);
        ESP_LOGI(TAG, "✅ [Gemini TTS] Success: %zu bytes audio generated (%zu samples)", 
                 decoded_len, *samples_written);
        return ESP_OK;
    }
    
    cJSON_Delete(response_json);
    ESP_LOGE(TAG, "❌ [Gemini TTS] Failed to extract audioContent from response");
    return ESP_FAIL;
}

/**
 * Context structure for streaming TTS with callback
 */
typedef struct {
    streaming_base64_decoder_t decoder;
    gemini_tts_playback_callback_t callback;
    void *user_data;
    const char *base64_audio;  // Pointer to base64 string in response JSON
    size_t base64_pos;         // Current position in base64_audio
    size_t base64_len;         // Total length of base64_audio
    uint8_t decode_buf[1024];  // Buffer for decoded PCM chunks
    bool in_audio_content;     // Are we inside audioContent field?
} streaming_tts_context_t;

esp_err_t gemini_tts_streaming(const char *text, gemini_tts_playback_callback_t callback, void *user_data)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!text || !callback) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "🔊 [Gemini TTS Streaming] Generating speech: \"%.100s%s\"",
             text, strlen(text) > 100 ? "..." : "");

    // Build JSON request for Google Cloud Text-to-Speech API
    cJSON *root = cJSON_CreateObject();
    cJSON *input = cJSON_CreateObject();
    cJSON *voice = cJSON_CreateObject();
    cJSON *audioConfig = cJSON_CreateObject();

    cJSON_AddItemToObject(root, "input", input);
    cJSON_AddItemToObject(root, "voice", voice);
    cJSON_AddItemToObject(root, "audioConfig", audioConfig);

    cJSON_AddStringToObject(input, "text", text);
    cJSON_AddStringToObject(voice, "languageCode", "en-US");
    cJSON_AddStringToObject(voice, "name", "en-US-Neural2-D");
    cJSON_AddStringToObject(audioConfig, "audioEncoding", "LINEAR16");
    cJSON_AddNumberToObject(audioConfig, "sampleRateHertz", 24000);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        ESP_LOGE(TAG, "Failed to create JSON payload");
        return ESP_ERR_NO_MEM;
    }

    char url[512];
    snprintf(url, sizeof(url),
             "https://texttospeech.googleapis.com/v1/text:synthesize?key=%s",
             s_config.api_key);

    // Perform HTTP request - using standard buffering for JSON response
    http_buffer_t http_response = {0};
    esp_err_t ret = http_post_json_with_auth(url, payload, NULL, &http_response);
    free(payload);

    if (ret != ESP_OK) {
        if (http_response.data) free(http_response.data);
        return ret;
    }

    // Parse response to extract base64 audio
    if (!http_response.data || http_response.len == 0) {
        ESP_LOGE(TAG, "Empty response");
        if (http_response.data) free(http_response.data);
        return ESP_FAIL;
    }

    // Null-terminate for JSON parsing (buffer has room since allocated with extra space)
    if (http_response.len < http_response.cap) {
        http_response.data[http_response.len] = '\0';
    } else {
        // Buffer is exactly full - shouldn't happen with our allocation strategy
        // but safe handling here
        ESP_LOGE(TAG, "Response buffer is full, cannot null-terminate");
        free(http_response.data);
        return ESP_ERR_NO_MEM;
    }

    // Verify response looks like valid JSON before parsing
    ESP_LOGD(TAG, "Response buffer: len=%u, cap=%u, first_char='%c'",
             http_response.len, http_response.cap, http_response.data[0]);

    cJSON *response_json = cJSON_Parse((char *)http_response.data);

    if (!response_json) {
        ESP_LOGE(TAG, "Failed to parse JSON response (len=%u, cap=%u)", http_response.len, http_response.cap);
        // Log first 200 chars of response for debugging
        if (http_response.data && http_response.len > 0) {
            char debug_buf[201];
            size_t debug_len = (http_response.len > 200) ? 200 : http_response.len;
            memcpy(debug_buf, http_response.data, debug_len);
            debug_buf[debug_len] = '\0';
            ESP_LOGI(TAG, "Response start: %.200s", debug_buf);
        }
        free(http_response.data);
        return ESP_FAIL;
    }

    free(http_response.data);

    cJSON *audioContent = cJSON_GetObjectItem(response_json, "audioContent");
    if (audioContent && cJSON_IsString(audioContent)) {
        const char *base64_audio = audioContent->valuestring;
        size_t base64_len = strlen(base64_audio);

        // Initialize streaming decoder
        streaming_base64_decoder_t decoder;
        streaming_base64_decoder_init(&decoder);

        // Process base64 in chunks, streaming decoded audio
        uint8_t decode_buf[1024];
        size_t pos = 0;
        size_t total_samples = 0;

        while (pos < base64_len) {
            // Take up to 256 base64 characters at a time (decodes to ~192 bytes = 96 PCM samples)
            size_t chunk_size = (base64_len - pos > 256) ? 256 : (base64_len - pos);

            size_t decode_buf_size = sizeof(decode_buf);
            esp_err_t decode_ret = streaming_base64_decode(
                &decoder,
                (const uint8_t *)(base64_audio + pos),
                chunk_size,
                decode_buf,
                &decode_buf_size);

            if (decode_ret != ESP_OK) {
                ESP_LOGE(TAG, "Base64 decode failed at position %zu", pos);
                cJSON_Delete(response_json);
                return ESP_FAIL;
            }

            // Stream decoded PCM to callback
            if (decode_buf_size > 0) {
                size_t sample_count = decode_buf_size / sizeof(int16_t);
                esp_err_t callback_ret = callback((int16_t *)decode_buf, sample_count, user_data);
                if (callback_ret != ESP_OK) {
                    ESP_LOGW(TAG, "Callback returned error, stopping streaming");
                    cJSON_Delete(response_json);
                    return callback_ret;
                }
                total_samples += sample_count;
            }

            pos += chunk_size;
        }

        // Finalize any remaining bytes
        size_t final_size = sizeof(decode_buf);
        esp_err_t final_ret = streaming_base64_decode_finish(&decoder, decode_buf, &final_size);
        if (final_ret == ESP_OK && final_size > 0) {
            size_t final_samples = final_size / sizeof(int16_t);
            callback((int16_t *)decode_buf, final_samples, user_data);
            total_samples += final_samples;
        }

        cJSON_Delete(response_json);
        ESP_LOGI(TAG, "✅ [Gemini TTS Streaming] Success: %zu samples streamed (%.2f seconds at 24kHz)",
                 total_samples, (float)total_samples / 24000.0f);
        return ESP_OK;
    }

    cJSON_Delete(response_json);
    ESP_LOGE(TAG, "❌ [Gemini TTS Streaming] Failed to extract audioContent from response");
    return ESP_FAIL;
}

void gemini_api_deinit(void)
{
    memset(&s_config, 0, sizeof(s_config));
    s_initialized = false;
    ESP_LOGI(TAG, "Gemini API deinitialized");
}
