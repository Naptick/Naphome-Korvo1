#include "korvo1.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "korvo1";

__attribute__((weak)) esp_err_t korvo1_i2s_read(i2s_port_t port, void *dest, size_t size, size_t *bytes_read, TickType_t ticks_to_wait)
{
    return i2s_read(port, dest, size, bytes_read, ticks_to_wait);
}

esp_err_t korvo1_init(korvo1_t *dev, const korvo1_config_t *config)
{
    ESP_RETURN_ON_FALSE(dev && config, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    memcpy(&dev->config, config, sizeof(korvo1_config_t));

    i2s_channel_fmt_t ch_fmt;
    if (config->channel_format == I2S_CHANNEL_FMT_ONLY_LEFT) {
        ch_fmt = I2S_CHANNEL_FMT_ONLY_LEFT;
    } else if (config->channel_format == I2S_CHANNEL_FMT_ONLY_RIGHT) {
        ch_fmt = I2S_CHANNEL_FMT_ONLY_RIGHT;
    } else {
        ch_fmt = I2S_CHANNEL_FMT_RIGHT_LEFT; // STEREO
    }
    
    i2s_config_t i2s_conf = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,  // Standard I2S RX (not PDM) - Korvo1 uses ES7210 ADC
        .sample_rate = config->sample_rate_hz > 0 ? config->sample_rate_hz : 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = ch_fmt,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = config->dma_buffer_count > 0 ? config->dma_buffer_count : 4,
        .dma_buf_len = config->dma_buffer_len > 0 ? config->dma_buffer_len : 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    const char* ch_fmt_str = (ch_fmt == I2S_CHANNEL_FMT_ONLY_LEFT) ? "ONLY_LEFT" :
                             (ch_fmt == I2S_CHANNEL_FMT_ONLY_RIGHT) ? "ONLY_RIGHT" : "STEREO";
    ESP_LOGI(TAG, "I2S Configuration: mode=I2S_RX (standard), sample_rate=%d, channel_format=%s, dma_bufs=%d x %d",
             (int)i2s_conf.sample_rate, ch_fmt_str, (int)i2s_conf.dma_buf_count, (int)i2s_conf.dma_buf_len);
    ESP_RETURN_ON_ERROR(i2s_driver_install(config->port, &i2s_conf, 0, NULL), TAG, "driver install failed");

    i2s_pin_config_t pin_conf = {
        .mck_io_num = config->mclk_io_num,
        .bck_io_num = config->bclk_io_num,
        .ws_io_num = config->ws_io_num,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = config->din_io_num
    };
    ESP_LOGI(TAG, "I2S Pin Configuration: DIN=GPIO%d, BCLK=GPIO%d, WS=GPIO%d, MCLK=GPIO%d",
             pin_conf.data_in_num, pin_conf.bck_io_num, pin_conf.ws_io_num, pin_conf.mck_io_num);
    ESP_RETURN_ON_ERROR(i2s_set_pin(config->port, &pin_conf), TAG, "set pin failed");
    ESP_RETURN_ON_ERROR(i2s_zero_dma_buffer(config->port), TAG, "zero dma buffer failed");
    ESP_LOGI(TAG, "I2S driver installed and pins configured successfully");

    dev->initialized = true;
    dev->streaming = false;
    return ESP_OK;
}

esp_err_t korvo1_start(korvo1_t *dev)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(dev->initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_ERROR(i2s_start(dev->config.port), TAG, "start stream failed");
    dev->streaming = true;
    return ESP_OK;
}

esp_err_t korvo1_stop(korvo1_t *dev)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    if (!dev->initialized) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(i2s_stop(dev->config.port), TAG, "stop stream failed");
    dev->streaming = false;
    return ESP_OK;
}

esp_err_t korvo1_read(korvo1_t *dev, void *buffer, size_t bytes_to_read, size_t *bytes_read, TickType_t ticks_to_wait)
{
    ESP_RETURN_ON_FALSE(dev && buffer, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(dev->initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(dev->streaming, ESP_ERR_INVALID_STATE, TAG, "stream not started");

    return korvo1_i2s_read(dev->config.port, buffer, bytes_to_read, bytes_read, ticks_to_wait);
}

esp_err_t korvo1_deinit(korvo1_t *dev)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    if (!dev->initialized) {
        return ESP_OK;
    }
    if (dev->streaming) {
        i2s_stop(dev->config.port);
    }
    i2s_driver_uninstall(dev->config.port);
    dev->initialized = false;
    dev->streaming = false;
    return ESP_OK;
}
