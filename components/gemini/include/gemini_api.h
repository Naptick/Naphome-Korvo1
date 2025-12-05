#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Gemini API configuration
 */
typedef struct {
    char api_key[128];  // Google Gemini API key
    char model[64];     // Model name (e.g., "gemini-1.5-flash" or "gemini-1.5-pro")
} gemini_config_t;

/**
 * Initialize Gemini API client
 * @param config: API configuration
 * @return ESP_OK on success
 */
esp_err_t gemini_api_init(const gemini_config_t *config);

/**
 * Speech-to-Text: Convert audio to text using Gemini
 * @param audio_data: PCM audio samples (16-bit, 16kHz mono)
 * @param audio_len: Number of samples
 * @param text_out: Buffer to store transcribed text
 * @param text_len: Size of text buffer
 * @return ESP_OK on success
 */
esp_err_t gemini_stt(const int16_t *audio_data, size_t audio_len, char *text_out, size_t text_len);

/**
 * LLM: Send text prompt and get response
 * @param prompt: Input text prompt
 * @param response: Buffer to store LLM response
 * @param response_len: Size of response buffer
 * @return ESP_OK on success
 */
esp_err_t gemini_llm(const char *prompt, char *response, size_t response_len);

/**
 * Function call result from LLM
 */
typedef struct {
    char function_name[64];
    char arguments[512];  // JSON string
    bool is_function_call;
} gemini_function_call_t;

/**
 * LLM with function calling support
 * @param prompt: Input text prompt
 * @param tools_json: JSON string defining available functions/tools (NULL if none)
 * @param response: Buffer to store LLM text response
 * @param response_len: Size of response buffer
 * @param function_call: Output function call if LLM wants to call a function (can be NULL)
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if function call detected (check function_call)
 */
esp_err_t gemini_llm_with_functions(const char *prompt, const char *tools_json,
                                     char *response, size_t response_len,
                                     gemini_function_call_t *function_call);

/**
 * Text-to-Speech: Convert text to audio using Gemini
 * @param text: Text to synthesize
 * @param audio_out: Buffer to store PCM audio samples
 * @param audio_len: Size of audio buffer (in samples)
 * @param samples_written: Number of samples actually written
 * @return ESP_OK on success
 */
esp_err_t gemini_tts(const char *text, int16_t *audio_out, size_t audio_len, size_t *samples_written);

/**
 * Callback function for streaming TTS audio playback
 * Called as decoded PCM chunks become available
 * @param samples: PCM audio samples (16-bit, mono)
 * @param sample_count: Number of samples in this chunk
 * @param user_data: User context pointer
 * @return ESP_OK to continue, or error to stop streaming
 */
typedef esp_err_t (*gemini_tts_playback_callback_t)(const int16_t *samples, size_t sample_count, void *user_data);

/**
 * Text-to-Speech with streaming playback
 * Decodes and streams audio to callback as it arrives from the API
 * @param text: Text to synthesize
 * @param callback: Function to call with decoded audio chunks
 * @param user_data: User context passed to callback
 * @return ESP_OK on success
 */
esp_err_t gemini_tts_streaming(const char *text, gemini_tts_playback_callback_t callback, void *user_data);

/**
 * Deinitialize Gemini API client
 */
void gemini_api_deinit(void);

#ifdef __cplusplus
}
#endif
