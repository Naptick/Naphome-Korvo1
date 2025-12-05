#include "mp3_decoder.h"
#include "minimp3.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "mp3_decoder";

struct mp3_decoder {
    mp3dec_t dec;
    mp3dec_frame_info_t info;
    bool initialized;
    size_t bytes_consumed;  // Track bytes consumed from last decode
};

mp3_decoder_t *mp3_decoder_create(void)
{
    // Allocate decoder state in internal RAM (not PSRAM) for interrupt safety
    // I2S DMA interrupts may access decoder state indirectly
    mp3_decoder_t *decoder = heap_caps_malloc(sizeof(mp3_decoder_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!decoder) {
        // Fallback to regular malloc if internal RAM allocation fails
        decoder = calloc(1, sizeof(mp3_decoder_t));
        if (!decoder) {
            ESP_LOGE(TAG, "Failed to allocate decoder");
            return NULL;
        }
    } else {
        // Zero-initialize if using heap_caps_malloc
        memset(decoder, 0, sizeof(mp3_decoder_t));
    }
    
    mp3dec_init(&decoder->dec);
    decoder->initialized = true;
    ESP_LOGI(TAG, "MP3 decoder created");
    return decoder;
}

void mp3_decoder_destroy(mp3_decoder_t *decoder)
{
    if (decoder) {
        free(decoder);
    }
}

esp_err_t mp3_decoder_decode(mp3_decoder_t *decoder,
                             const uint8_t *mp3_data,
                             size_t mp3_len,
                             int16_t *pcm_out,
                             size_t pcm_out_size,
                             size_t *samples_decoded,
                             int *sample_rate,
                             int *channels,
                             size_t *bytes_consumed)
{
    if (!decoder || !mp3_data || !pcm_out || !samples_decoded || !sample_rate || !channels) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!decoder->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (mp3_len == 0) {
        *samples_decoded = 0;
        return ESP_OK;
    }
    
    // Decode MP3 frame
    int samples = mp3dec_decode_frame(&decoder->dec, mp3_data, (int)mp3_len, pcm_out, &decoder->info);
    
    if (samples < 0) {
        // Negative return means error or need more data
        decoder->bytes_consumed = 0;
        *samples_decoded = 0;
        if (bytes_consumed) {
            *bytes_consumed = 0;
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    if (samples == 0) {
        // No samples decoded (might need more data or invalid frame)
        decoder->bytes_consumed = 0;
        *samples_decoded = 0;
        if (bytes_consumed) {
            *bytes_consumed = 0;
        }
        return ESP_OK;
    }
    
    // Check if output buffer is large enough
    size_t required_samples = (size_t)samples * decoder->info.channels;
    if (required_samples > pcm_out_size) {
        ESP_LOGW(TAG, "Output buffer too small: need %zu, have %zu", required_samples, pcm_out_size);
        decoder->bytes_consumed = 0;
        if (bytes_consumed) {
            *bytes_consumed = 0;
        }
        return ESP_ERR_NO_MEM;
    }
    
    // Store bytes consumed (frame_bytes tells us how many bytes were used)
    decoder->bytes_consumed = decoder->info.frame_bytes;
    
    *samples_decoded = required_samples;
    *sample_rate = decoder->info.hz;
    *channels = decoder->info.channels;
    if (bytes_consumed) {
        *bytes_consumed = decoder->bytes_consumed;
    }
    
    return ESP_OK;
}
