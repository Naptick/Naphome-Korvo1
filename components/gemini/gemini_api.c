#include "gemini_api.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_tls.h"
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "esp_heap_caps.h"
#include "tls_mutex.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#include <math.h>

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
            // Check if buffer would overflow (leave 1 byte for null terminator)
            if (buf->len + evt->data_len >= buf->cap) {
                // Don't log in interrupt handler - it blocks the main task from watchdog
                // Pre-allocated buffer should be sufficient for Gemini API responses
                ESP_LOGW(TAG, "Response buffer overflow: len=%zu, adding=%zu, cap=%zu", 
                         buf->len, evt->data_len, buf->cap);
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
    
    // For large WAV files (>64KB), prefer PSRAM to avoid stack/heap issues
    uint8_t *wav = NULL;
    if (total_bytes > 64 * 1024) {
        wav = (uint8_t *)heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!wav) {
            ESP_LOGW(TAG, "PSRAM allocation failed for WAV (%zu bytes), trying internal RAM", total_bytes);
        }
    }
    
    // Fallback to internal RAM if PSRAM failed or buffer is small
    if (!wav) {
        wav = (uint8_t *)malloc(total_bytes);
    }
    
    if (!wav) {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes for WAV file", total_bytes);
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

// Base64 encode with allocation (prefer PSRAM for large buffers)
static esp_err_t base64_encode_alloc(const uint8_t *input, size_t input_len, char **out_str)
{
    if (!input || !out_str) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t target_len = ((input_len + 2) / 3) * 4 + 1;
    
    // For large buffers (>64KB), prefer PSRAM
    char *encoded = NULL;
    if (target_len > 64 * 1024) {
        encoded = (char *)heap_caps_malloc(target_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!encoded) {
            ESP_LOGW(TAG, "PSRAM allocation failed for base64 buffer (%zu bytes), trying internal RAM", target_len);
        }
    }
    
    // Fallback to internal RAM if PSRAM failed or buffer is small
    if (!encoded) {
        encoded = (char *)malloc(target_len);
    }
    
    if (!encoded) {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes for base64 encoding", target_len);
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
    // Optimized: Use PSRAM for all large buffers to preserve internal RAM for TLS
    size_t RESPONSE_BUFFER_SIZE = 96 * 1024;  // Reduced to 96KB for LLM (PSRAM)

    // Check if this is a TTS request (use large buffer)
    if (strstr(url, "texttospeech") != NULL) {
        RESPONSE_BUFFER_SIZE = 192 * 1024;  // TTS: 192KB (reduced, use PSRAM)
    }

    if (!response->data) {
        // Always prefer PSRAM for response buffers (>32KB) to preserve internal RAM for TLS
        // TLS connections need internal RAM for SHA buffers and SSL contexts
        if (RESPONSE_BUFFER_SIZE > 32 * 1024) {
            response->data = heap_caps_malloc(RESPONSE_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!response->data) {
                ESP_LOGW(TAG, "PSRAM allocation failed, trying internal RAM");
                response->data = heap_caps_malloc(RESPONSE_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
        } else {
            // Small buffers can use regular malloc (will prefer PSRAM if SPIRAM_USE_MALLOC=y)
            response->data = malloc(RESPONSE_BUFFER_SIZE);
        }
        
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
                ESP_LOGI(TAG, "Using fallback buffer: %zu bytes", FALLBACK_SIZE);
            }
        } else {
            response->cap = RESPONSE_BUFFER_SIZE;
            ESP_LOGI(TAG, "Successfully allocated %zu byte response buffer", RESPONSE_BUFFER_SIZE);
        }
        response->len = 0;
        ESP_LOGD(TAG, "Allocated response buffer: %zu bytes", response->cap);
    }

    // TODO: Re-enable certificate verification once certificate bundle issue is resolved
    // Currently skipping due to PK verify errors (0x4290) - certificate signature verification failing
    // This is a temporary workaround for development
    ESP_LOGW(TAG, "⚠️  Development mode: Certificate verification disabled");
    
    // Configure HTTP client to skip certificate verification
    // CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y in sdkconfig.defaults enables
    // MBEDTLS_SSL_VERIFY_NONE when no CA cert is provided, avoiding the
    // "No server verification option set" error
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = response,
        .timeout_ms = 30000,
        .skip_cert_common_name_check = true,  // Skip certificate verification for development
        .crt_bundle_attach = NULL,  // Don't use certificate bundle
        .use_global_ca_store = false,  // Don't use global CA store
        .is_async = false,
        .disable_auto_redirect = false,
    };
    
    // Acquire TLS mutex to serialize TLS connections
    // This prevents concurrent TLS connections that cause memory allocation errors
    esp_err_t mutex_err = tls_mutex_take(pdMS_TO_TICKS(10000));  // 10 second timeout
    if (mutex_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire TLS mutex: %s", esp_err_to_name(mutex_err));
        return ESP_ERR_TIMEOUT;
    }
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        tls_mutex_give();  // Release mutex on error
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
    
    // Release TLS mutex after connection is complete
    tls_mutex_give();

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
    // Use simpler log format to avoid potential stack issues with large values
    ESP_LOGI(TAG, "[Gemini STT] Starting: %zu samples, %.1fs", audio_len, duration_sec);
    
    // Validate audio - check if it's all zeros (silence)
    int32_t sum = 0;
    int32_t sum_sq = 0;
    int16_t max_val = 0;
    int16_t min_val = 0;
    for (size_t i = 0; i < audio_len; i++) {
        int16_t sample = audio_data[i];
        sum += sample;
        sum_sq += (int32_t)sample * sample;
        if (sample > max_val) max_val = sample;
        if (sample < min_val) min_val = sample;
    }
    float rms = sqrtf((float)sum_sq / audio_len);
    float avg = (float)sum / audio_len;
    ESP_LOGI(TAG, "Audio stats: RMS=%.1f, avg=%.1f, peak=[%d, %d]", rms, avg, min_val, max_val);
    
    if (rms < 10.0f) {
        ESP_LOGW(TAG, "⚠️  Audio appears to be silence (RMS=%.1f < 10), STT may fail", rms);
    }
    
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
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(ret));
        if (response.data) {
            ESP_LOGE(TAG, "Response data (first 200 chars): %.200s", (const char *)response.data);
            free(response.data);
        }
        return ret;
    }
    
    // Parse response
    if (!response.data || response.len == 0) {
        ESP_LOGE(TAG, "Empty response from STT API");
        if (response.data) free(response.data);
        return ESP_FAIL;
    }
    
    response.data = realloc(response.data, response.len + 1);
    response.data[response.len] = '\0';
    
    ESP_LOGD(TAG, "STT API response (len=%zu, first 500 chars): %.500s", response.len, (const char *)response.data);
    
    cJSON *response_json = cJSON_Parse((char *)response.data);
    if (!response_json) {
        ESP_LOGE(TAG, "Failed to parse JSON response (len=%zu, first 200 chars: %.200s)", 
                 response.len, (const char *)response.data);
        free(response.data);
        return ESP_FAIL;
    }
    
    // Check for error in response
    cJSON *error = cJSON_GetObjectItem(response_json, "error");
    if (error) {
        cJSON *error_message = cJSON_GetObjectItem(error, "message");
        cJSON *error_code = cJSON_GetObjectItem(error, "code");
        ESP_LOGE(TAG, "STT API error: code=%d, message=%s", 
                 error_code ? error_code->valueint : -1,
                 error_message ? error_message->valuestring : "unknown");
        cJSON_Delete(response_json);
        free(response.data);
        return ESP_FAIL;
    }
    
    // Extract transcript
    cJSON *results = cJSON_GetObjectItem(response_json, "results");
    if (!results) {
        ESP_LOGE(TAG, "No 'results' field in STT response");
        cJSON_Delete(response_json);
        free(response.data);
        return ESP_FAIL;
    }
    
    if (!cJSON_IsArray(results)) {
        ESP_LOGE(TAG, "'results' is not an array");
        cJSON_Delete(response_json);
        free(response.data);
        return ESP_FAIL;
    }
    
    int results_size = cJSON_GetArraySize(results);
    ESP_LOGI(TAG, "STT response contains %d result(s)", results_size);
    
    if (results_size == 0) {
        ESP_LOGW(TAG, "⚠️  STT returned no results - audio may be silence or unrecognized");
        cJSON_Delete(response_json);
        free(response.data);
        // Return empty string instead of failure - this is a valid case (silence)
        text_out[0] = '\0';
        return ESP_OK;
    }
    
    cJSON *result = cJSON_GetArrayItem(results, 0);
    if (!result) {
        ESP_LOGE(TAG, "Failed to get first result");
        cJSON_Delete(response_json);
        free(response.data);
        return ESP_FAIL;
    }
    
    cJSON *alternatives = cJSON_GetObjectItem(result, "alternatives");
    if (!alternatives || !cJSON_IsArray(alternatives)) {
        ESP_LOGE(TAG, "No 'alternatives' array in result");
        cJSON_Delete(response_json);
        free(response.data);
        return ESP_FAIL;
    }
    
    int alt_size = cJSON_GetArraySize(alternatives);
    if (alt_size == 0) {
        ESP_LOGW(TAG, "No alternatives in result");
        cJSON_Delete(response_json);
        free(response.data);
        text_out[0] = '\0';
        return ESP_OK;
    }
    
    cJSON *alt = cJSON_GetArrayItem(alternatives, 0);
    if (!alt) {
        ESP_LOGE(TAG, "Failed to get first alternative");
        cJSON_Delete(response_json);
        free(response.data);
        return ESP_FAIL;
    }
    
    cJSON *transcript = cJSON_GetObjectItem(alt, "transcript");
    if (!transcript || !cJSON_IsString(transcript)) {
        ESP_LOGE(TAG, "No 'transcript' string in alternative");
        cJSON_Delete(response_json);
        free(response.data);
        return ESP_FAIL;
    }
    
    strncpy(text_out, transcript->valuestring, text_len - 1);
    text_out[text_len - 1] = '\0';
    cJSON_Delete(response_json);
    free(response.data);
    ESP_LOGI(TAG, "✅ [Gemini STT] Success: \"%s\"", text_out);
    return ESP_OK;
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

esp_err_t gemini_llm_with_functions(const char *prompt, const char *tools_json,
                                     char *response, size_t response_len,
                                     gemini_function_call_t *function_call)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!prompt || !response) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize function_call output
    if (function_call) {
        memset(function_call, 0, sizeof(gemini_function_call_t));
    }
    
    ESP_LOGI(TAG, "💬 [Gemini LLM] Generating response with functions: \"%.100s%s\"", 
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
    
    // Add tools if provided
    if (tools_json && strlen(tools_json) > 0) {
        cJSON *tools = cJSON_Parse(tools_json);
        if (tools) {
            cJSON_AddItemToObject(root, "tools", tools);
            ESP_LOGI(TAG, "Added function definitions to request");
        } else {
            ESP_LOGW(TAG, "Failed to parse tools_json, continuing without functions");
        }
    }
    
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
    
    // Extract text or function call from Gemini response
    cJSON *candidates = cJSON_GetObjectItem(response_json, "candidates");
    if (candidates && cJSON_IsArray(candidates) && cJSON_GetArraySize(candidates) > 0) {
        cJSON *candidate = cJSON_GetArrayItem(candidates, 0);
        if (candidate) {
            cJSON *content_obj = cJSON_GetObjectItem(candidate, "content");
            if (content_obj) {
                cJSON *parts = cJSON_GetObjectItem(content_obj, "parts");
                if (parts && cJSON_IsArray(parts) && cJSON_GetArraySize(parts) > 0) {
                    // Check for function call first
                    cJSON *part = cJSON_GetArrayItem(parts, 0);
                    if (part) {
                        cJSON *function_call_obj = cJSON_GetObjectItem(part, "functionCall");
                        if (function_call_obj && function_call) {
                            // Function call detected
                            cJSON *name = cJSON_GetObjectItem(function_call_obj, "name");
                            cJSON *args = cJSON_GetObjectItem(function_call_obj, "args");
                            
                            if (name && cJSON_IsString(name)) {
                                strncpy(function_call->function_name, name->valuestring,
                                       sizeof(function_call->function_name) - 1);
                                
                                if (args) {
                                    char *args_str = cJSON_PrintUnformatted(args);
                                    if (args_str) {
                                        strncpy(function_call->arguments, args_str,
                                               sizeof(function_call->arguments) - 1);
                                        free(args_str);
                                    }
                                }
                                
                                function_call->is_function_call = true;
                                cJSON_Delete(response_json);
                                ESP_LOGI(TAG, "🔧 [Gemini LLM] Function call detected: %s", 
                                         function_call->function_name);
                                return ESP_ERR_NOT_FOUND;  // Special return to indicate function call
                            }
                        }
                        
                        // No function call, extract text
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
    ESP_LOGE(TAG, "❌ [Gemini LLM] Failed to extract text or function call from response");
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

esp_err_t gemini_tts_streaming(const char *text, gemini_tts_playback_callback_t callback, void *user_data)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!text || !callback) {
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

    // Allocate large PSRAM buffer for entire JSON response
    // TTS responses are typically 50-200KB JSON with base64-encoded audio
    // Time announcements can be longer, so increase buffer size
    // Leave 1 byte for null terminator
    const size_t JSON_BUFFER_SIZE = 512 * 1024;  // 512KB in PSRAM (increased for longer TTS responses like time announcements)
    http_buffer_t response = {0};
    response.data = heap_caps_malloc(JSON_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!response.data) {
        ESP_LOGW(TAG, "PSRAM allocation failed, trying internal RAM");
        response.data = heap_caps_malloc(JSON_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!response.data) {
            ESP_LOGE(TAG, "Failed to allocate %zu bytes for TTS response", JSON_BUFFER_SIZE);
            free(payload);
            return ESP_ERR_NO_MEM;
        }
    }
    response.cap = JSON_BUFFER_SIZE - 1;  // Reserve 1 byte for null terminator
    response.len = 0;
    ESP_LOGI(TAG, "Allocated %zu byte PSRAM buffer for TTS response (cap=%zu)", 
             JSON_BUFFER_SIZE, response.cap);

    // Perform HTTP request - store entire response
    esp_err_t err = http_post_json_with_auth(url, payload, NULL, &response);
    free(payload);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
        free(response.data);
        return err;
    }

    // Ensure response is null-terminated for JSON parsing
    if (response.len >= response.cap) {
        ESP_LOGE(TAG, "Response buffer full (%zu >= %zu), may be truncated", response.len, response.cap);
        free(response.data);
        return ESP_ERR_NO_MEM;
    }
    
    // Null-terminate the response for safe JSON parsing
    response.data[response.len] = '\0';
    
    // Debug: Check for potential issues in response
    ESP_LOGD(TAG, "TTS response: len=%zu, cap=%zu, last 50 chars: %.50s", 
             response.len, response.cap, (const char *)(response.data + (response.len > 50 ? response.len - 50 : 0)));
    
    // Validate response doesn't contain embedded nulls (which would break JSON parsing)
    bool has_null = false;
    for (size_t i = 0; i < response.len; i++) {
        if (response.data[i] == '\0') {
            has_null = true;
            ESP_LOGW(TAG, "Response contains null byte at offset %zu", i);
            break;
        }
    }
    if (has_null) {
        ESP_LOGE(TAG, "Response contains embedded null bytes - cannot parse as JSON");
        free(response.data);
        return ESP_FAIL;
    }
    
    // Try to parse JSON - if it fails due to long base64 string, extract manually
    cJSON *json_root = cJSON_Parse((const char *)response.data);
    const char *base64_audio = NULL;
    size_t base64_len = 0;
    
    if (!json_root) {
        // JSON parsing failed - try manual extraction as fallback
        ESP_LOGW(TAG, "cJSON parse failed, trying manual extraction");
        
        // Ensure response is treated as a string for searching
        const char *response_str = (const char *)response.data;
        
        // Search for "audioContent" in the response
        const char *audio_content_start = strstr(response_str, "\"audioContent\"");
        if (!audio_content_start) {
            // Try case-insensitive search
            for (size_t i = 0; i < response.len - 13; i++) {
                if (strncasecmp(response_str + i, "\"audioContent\"", 14) == 0) {
                    audio_content_start = response_str + i;
                    break;
                }
            }
        }
        
        if (audio_content_start) {
            // Find the colon after "audioContent"
            const char *colon = strchr(audio_content_start, ':');
            if (colon && colon < response_str + response.len) {
                // Skip whitespace after colon
                const char *value_start = colon + 1;
                while (value_start < response_str + response.len && 
                       (*value_start == ' ' || *value_start == '\t' || *value_start == '\n' || *value_start == '\r')) {
                    value_start++;
                }
                
                // Find opening quote
                if (value_start < response_str + response.len && *value_start == '"') {
                    base64_audio = value_start + 1; // Start after opening quote
                    
                    // Find closing quote - search forward from start
                    const char *quote_end = NULL;
                    for (const char *p = base64_audio; p < response_str + response.len; p++) {
                        if (*p == '"' && (p == base64_audio || *(p-1) != '\\')) {
                            // Found unescaped quote
                            quote_end = p;
                            break;
                        }
                    }
                    
                    if (quote_end && quote_end > base64_audio) {
                        base64_len = quote_end - base64_audio;
                        
                        // Validate we're within response bounds
                        if (base64_audio + base64_len > response_str + response.len) {
                            ESP_LOGE(TAG, "Extracted base64 extends beyond response buffer");
                            free(response.data);
                            return ESP_FAIL;
                        }
                        
                        // Trim any leading/trailing whitespace or control characters
                        while (base64_len > 0 && base64_audio < response_str + response.len && 
                               (base64_audio[0] == ' ' || base64_audio[0] == '\t' || 
                                base64_audio[0] == '\n' || base64_audio[0] == '\r' || 
                                (unsigned char)base64_audio[0] < 0x20)) {
                            base64_audio++;
                            base64_len--;
                        }
                        while (base64_len > 0 && base64_audio + base64_len - 1 < response_str + response.len &&
                               (base64_audio[base64_len - 1] == ' ' || 
                                base64_audio[base64_len - 1] == '\t' || 
                                base64_audio[base64_len - 1] == '\n' || 
                                base64_audio[base64_len - 1] == '\r' ||
                                (unsigned char)base64_audio[base64_len - 1] < 0x20)) {
                            base64_len--;
                        }
                        
                        // Verify we're still pointing to valid memory
                        if (base64_audio < response_str || base64_audio + base64_len > response_str + response.len) {
                            ESP_LOGE(TAG, "Base64 pointer out of bounds after trimming");
                            free(response.data);
                            return ESP_FAIL;
                        }
                        
                        ESP_LOGI(TAG, "Manually extracted base64: %zu chars (after trimming), offset: %zu", 
                                base64_len, base64_audio - response_str);
                    } else {
                        ESP_LOGE(TAG, "Could not find closing quote for audioContent");
                    }
                } else {
                    ESP_LOGE(TAG, "audioContent value does not start with quote (found: 0x%02x)", 
                            value_start < response_str + response.len ? (unsigned char)*value_start : 0);
                }
            } else {
                ESP_LOGE(TAG, "Could not find colon after audioContent");
            }
        } else {
            ESP_LOGE(TAG, "Could not find \"audioContent\" in response");
        }
        
        if (!base64_audio || base64_len == 0) {
            const char *error_ptr = cJSON_GetErrorPtr();
            if (error_ptr != NULL) {
                size_t error_offset = error_ptr - (const char *)response.data;
                ESP_LOGE(TAG, "JSON parse error at offset %zu", error_offset);
                size_t context_start = (error_offset > 100) ? error_offset - 100 : 0;
                size_t context_len = (error_offset + 100 < response.len) ? 200 : (response.len - context_start);
                ESP_LOGE(TAG, "Context around error: %.200s", (const char *)(response.data + context_start));
            }
            ESP_LOGE(TAG, "Failed to parse JSON and manual extraction failed (len=%zu)", response.len);
            ESP_LOGE(TAG, "First 200 chars: %.200s", (const char *)response.data);
            ESP_LOGE(TAG, "Last 200 chars: %.200s", (const char *)(response.data + (response.len > 200 ? response.len - 200 : 0)));
            free(response.data);
            return ESP_FAIL;
        }
    } else {
        // JSON parsed successfully - use cJSON to extract
        cJSON *audio_content = cJSON_GetObjectItem(json_root, "audioContent");
        if (!audio_content || !cJSON_IsString(audio_content)) {
            ESP_LOGE(TAG, "Missing or invalid audioContent in response");
            cJSON_Delete(json_root);
            free(response.data);
            return ESP_FAIL;
        }
        base64_audio = audio_content->valuestring;
        base64_len = strlen(base64_audio);
        cJSON_Delete(json_root);
    }
    ESP_LOGI(TAG, "Extracted base64 audio: %zu characters", base64_len);
    
    // Validate base64 string
    if (base64_len == 0) {
        ESP_LOGE(TAG, "Base64 string is empty");
        free(response.data);
        return ESP_FAIL;
    }
    
    // Validate pointer is within response buffer bounds and clamp length
    const char *response_start = (const char *)response.data;
    const char *response_end = response_start + response.len;
    
    // Check if pointer is within bounds
    bool pointer_in_bounds = (base64_audio >= response_start && base64_audio < response_end);
    
    if (!pointer_in_bounds) {
        ESP_LOGE(TAG, "Base64 pointer out of bounds: base64_audio=%p, response_start=%p, response_end=%p, offset=%ld",
                 base64_audio, response_start, response_end, 
                 base64_audio > response_start ? (long)(base64_audio - response_start) : -1);
        free(response.data);
        return ESP_FAIL;
    }
    
    // Calculate actual available space from base64_audio to end of buffer
    size_t available_space = response_end - base64_audio;
    
    // Clamp base64_len to available space if it exceeds
    if (base64_len > available_space) {
        ESP_LOGW(TAG, "Base64 length (%zu) exceeds available space (%zu), clamping to %zu", 
                 base64_len, available_space, available_space);
        base64_len = available_space;
    }
    
    if (base64_len == 0) {
        ESP_LOGE(TAG, "Base64 string is empty after bounds check");
        free(response.data);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Base64 audio validated: %zu characters (pointer=%p, buffer=[%p-%p], offset=%zu, available=%zu)", 
             base64_len, base64_audio, response_start, response_end,
             base64_audio - response_start, available_space);
    
    // Debug: Log first few bytes as hex to see what we're getting
    ESP_LOGD(TAG, "First 20 bytes of extracted base64 (hex):");
    for (size_t i = 0; i < (base64_len < 20 ? base64_len : 20); i++) {
        ESP_LOGD(TAG, "  [%zu] 0x%02x '%c'", i, (unsigned char)base64_audio[i], 
                 (base64_audio[i] >= 32 && base64_audio[i] < 127) ? base64_audio[i] : '.');
    }
    
    // Check first few characters are valid base64
    bool valid_base64 = true;
    size_t check_len = (base64_len < 50 ? base64_len : 50);  // Check more characters
    for (size_t i = 0; i < check_len; i++) {
        char c = base64_audio[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
              (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=')) {
            valid_base64 = false;
            ESP_LOGE(TAG, "Invalid base64 character at offset %zu: 0x%02x ('%c')", i, (unsigned char)c, 
                     (c >= 32 && c < 127) ? c : '.');
            
            // Log context around the error
            size_t context_start = (i > 20) ? i - 20 : 0;
            size_t context_len = (i + 20 < base64_len) ? 40 : (base64_len - context_start);
            ESP_LOGE(TAG, "Context around invalid char (offset %zu):", i);
            for (size_t j = context_start; j < context_start + context_len && j < base64_len; j++) {
                char ch = base64_audio[j];
                ESP_LOGE(TAG, "  [%zu] 0x%02x '%c'", j, (unsigned char)ch, (ch >= 32 && ch < 127) ? ch : '.');
            }
            break;
        }
    }
    if (!valid_base64) {
        ESP_LOGE(TAG, "Base64 string contains invalid characters");
        ESP_LOGE(TAG, "Base64 pointer: %p, Response buffer: %p-%p", 
                 base64_audio, response_start, response_end);
        ESP_LOGE(TAG, "First 100 bytes (hex):");
        for (size_t i = 0; i < (base64_len < 100 ? base64_len : 100); i++) {
            if (i % 16 == 0) {
                ESP_LOGE(TAG, "  %04zx:", i);
            }
            ESP_LOGE(TAG, " %02x", (unsigned char)base64_audio[i]);
            if (i % 16 == 15) {
                ESP_LOGE(TAG, "");
            }
        }
        if (base64_len > 100) {
            ESP_LOGE(TAG, "  ... (truncated, total %zu bytes)", base64_len);
        }
        free(response.data);
        return ESP_FAIL;
    }

    // Decode base64 to PCM audio - allocate in PSRAM
    // Base64 encoding increases size by ~33%, so decoded size is ~75% of base64 size
    // For safety, allocate full base64 size (will be smaller after decode)
    size_t audio_buffer_size = (base64_len * 3) / 4 + 1024;  // Add padding for safety
    int16_t *audio_samples = (int16_t *)heap_caps_malloc(audio_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!audio_samples) {
        ESP_LOGW(TAG, "PSRAM allocation for audio failed, trying internal RAM");
        audio_samples = (int16_t *)heap_caps_malloc(audio_buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!audio_samples) {
            ESP_LOGE(TAG, "Failed to allocate %zu bytes for decoded audio", audio_buffer_size);
            free(response.data);
            return ESP_ERR_NO_MEM;
        }
    }

    size_t decoded_len = 0;
    int mbedtls_ret = mbedtls_base64_decode((unsigned char *)audio_samples, audio_buffer_size, &decoded_len,
                                            (const unsigned char *)base64_audio, base64_len);
    free(response.data);

    if (mbedtls_ret != 0) {
        ESP_LOGE(TAG, "Base64 decode failed: %d (base64_len=%zu)", mbedtls_ret, base64_len);
        ESP_LOGE(TAG, "First 100 base64 chars: %.100s", base64_audio);
        ESP_LOGE(TAG, "Last 100 base64 chars: %.100s", base64_audio + (base64_len > 100 ? base64_len - 100 : 0));
        free(audio_samples);
        return ESP_FAIL;
    }

    size_t sample_count = decoded_len / sizeof(int16_t);
    ESP_LOGI(TAG, "Decoded %zu bytes (%zu samples) of PCM audio", decoded_len, sample_count);

    // Call callback with complete audio data
    esp_err_t callback_ret = callback(audio_samples, sample_count, user_data);
    free(audio_samples);

    if (callback_ret != ESP_OK) {
        ESP_LOGW(TAG, "TTS callback returned error: %s", esp_err_to_name(callback_ret));
        return callback_ret;
    }

    ESP_LOGI(TAG, "✅ [Gemini TTS] Complete - %zu samples delivered", sample_count);
    return ESP_OK;
}

void gemini_api_deinit(void)
{
    memset(&s_config, 0, sizeof(s_config));
    s_initialized = false;
    ESP_LOGI(TAG, "Gemini API deinitialized");
}
