#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "audio_player.h"
#include "wake_word_manager.h"
#include "voice_assistant.h"
#include "wifi_manager.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_indicators.h"
#include "action_manager.h"
#include "webserver.h"
#include "mdns.h"
#ifdef CONFIG_BT_ENABLED
#include "somnus_ble.h"
#endif
#include "sensor_integration.h"
#include "audio_file_manager.h"
#include "esp_task_wdt.h"
// WAV file is embedded via EMBED_FILES
// Access it via the generated symbol (ESP-IDF generates these symbols automatically)
extern const uint8_t _binary_256kMeasSweep_0_to_20000__12_dBFS_48k_Float_LR_refL_wav_start[] asm("_binary_256kMeasSweep_0_to_20000__12_dBFS_48k_Float_LR_refL_wav_start");
extern const uint8_t _binary_256kMeasSweep_0_to_20000__12_dBFS_48k_Float_LR_refL_wav_end[] asm("_binary_256kMeasSweep_0_to_20000__12_dBFS_48k_Float_LR_refL_wav_end");

// Offline welcome message (for use when internet is unavailable)
extern const uint8_t _binary_offline_welcome_wav_start[] asm("_binary_offline_welcome_wav_start");
extern const uint8_t _binary_offline_welcome_wav_end[] asm("_binary_offline_welcome_wav_end");
#include "mp3_decoder.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "driver/spi_common.h"
#include "driver/i2s.h"
#include <sys/stat.h>

static const char *TAG = "korvo1_led_audio";

// Webserver handle
static webserver_t *s_webserver = NULL;

// BLE WiFi connect callback
static bool ble_wifi_connect_cb(const char *ssid, const char *password, const char *user_token, bool is_production, void *ctx)
{
    (void)user_token;  // Not used for now
    (void)is_production;  // Not used for now
    (void)ctx;
    
    ESP_LOGI(TAG, "BLE: Connecting to WiFi: %s", ssid);
    
    wifi_manager_config_t wifi_cfg = {
        .ssid = {0},
        .password = {0}
    };
    strncpy(wifi_cfg.ssid, ssid, sizeof(wifi_cfg.ssid) - 1);
    strncpy(wifi_cfg.password, password, sizeof(wifi_cfg.password) - 1);
    
    esp_err_t err = wifi_manager_connect(&wifi_cfg);
    if (err == ESP_OK) {
        char ip_str[16];
        if (wifi_manager_get_ip(ip_str, sizeof(ip_str)) == ESP_OK) {
            ESP_LOGI(TAG, "BLE: WiFi connected, IP: %s", ip_str);
            
            // Initialize mDNS after BLE WiFi connection
            esp_err_t mdns_err = mdns_init();
            if (mdns_err == ESP_OK) {
                mdns_hostname_set("nap");
                mdns_instance_name_set("Korvo1 Voice Assistant");
                mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
                ESP_LOGI(TAG, "✅ mDNS configured for nap.local");
            }
            
            // Start webserver if not already started
            if (!s_webserver) {
                webserver_config_t ws_cfg = { .port = 80 };
                webserver_start(&s_webserver, &ws_cfg);
            }
            
            return true;
        }
    }
    
    ESP_LOGE(TAG, "BLE: WiFi connection failed: %s", esp_err_to_name(err));
    return false;
}

// BLE device command callback
static esp_err_t ble_device_command_cb(const char *payload, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "BLE: Received device command: %s", payload);
    return action_manager_execute_json(payload);
}

// Audio monitor for microphone input (used for LED reactivity)
static TaskHandle_t s_audio_monitor_task = NULL;

// LED strip handle
static led_strip_handle_t s_strip = NULL;

// Audio player configuration for korvo1
// Based on ES8311 codec on korvo1 board
// I2S pins per BSP: BCLK=GPIO40, LRCLK=GPIO41, DATA=GPIO39, MCLK=GPIO42
// I2C pins per BSP: SCL=GPIO2, SDA=GPIO1
static const audio_player_config_t s_audio_config = {
    .i2s_port = I2S_NUM_0,  // Korvo1 uses I2S0 for speaker
    .bclk_gpio = GPIO_NUM_40,  // BSP_I2S0_SCLK
    .lrclk_gpio = GPIO_NUM_41,  // BSP_I2S0_LCLK
    .data_gpio = GPIO_NUM_39,   // BSP_I2S0_DOUT
    .mclk_gpio = GPIO_NUM_42,   // BSP_I2S0_MCLK
    .i2c_scl_gpio = GPIO_NUM_2,  // BSP_I2C_SCL
    .i2c_sda_gpio = GPIO_NUM_1,  // BSP_I2C_SDA
    .default_sample_rate = CONFIG_AUDIO_SAMPLE_RATE,
};

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

// Generate a logarithmic frequency sweep (chirp) as PCM samples
static void generate_log_sweep(int16_t *samples, size_t num_samples, 
                                int sample_rate, 
                                float start_freq, float end_freq,
                                float duration_sec)
{
    const float two_pi = 2.0f * M_PI;
    const float log_start = logf(start_freq);
    const float log_end = logf(end_freq);
    const float log_range = log_end - log_start;
    const float sample_period = 1.0f / (float)sample_rate;
    const float amplitude = 0.3f; // 30% amplitude to avoid clipping
    const int16_t max_amplitude = (int16_t)(amplitude * 32767.0f);

    for (size_t i = 0; i < num_samples; i++) {
        float t = (float)i * sample_period;
        float normalized_t = t / duration_sec;
        
        // Logarithmic frequency sweep: f(t) = start * (end/start)^(t/T)
        float current_freq = start_freq * expf(log_range * normalized_t);
        
        // Generate sine wave at current frequency
        float phase = two_pi * current_freq * t;
        float sample = sinf(phase);
        
        // Convert to 16-bit integer
        samples[i] = (int16_t)(sample * max_amplitude);
    }
}

// Update LED animation based on audio playback progress
static void update_leds_for_audio(float progress, bool playing)
{
    if (!s_strip) {
        return;
    }

    if (!playing) {
        // All LEDs off when not playing
        for (uint32_t i = 0; i < CONFIG_LED_AUDIO_LED_COUNT; i++) {
            set_pixel_rgb(i, 0, 0, 0);
        }
        led_strip_refresh(s_strip);
        return;
    }

    // Animate LEDs based on progress: rainbow sweep
    // Calculate hue based on progress (0.0 to 1.0)
    float hue = fmodf(progress * 360.0f, 360.0f) / 360.0f;
    
    // Convert HSV to RGB
    float s = 1.0f; // Full saturation
    float v = 0.8f; // 80% brightness
    
    int hi = (int)(hue * 6.0f);
    float f = hue * 6.0f - hi;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    
    uint8_t r, g, b;
    switch (hi % 6) {
        case 0: r = (uint8_t)(v * 255); g = (uint8_t)(t * 255); b = (uint8_t)(p * 255); break;
        case 1: r = (uint8_t)(q * 255); g = (uint8_t)(v * 255); b = (uint8_t)(p * 255); break;
        case 2: r = (uint8_t)(p * 255); g = (uint8_t)(v * 255); b = (uint8_t)(t * 255); break;
        case 3: r = (uint8_t)(p * 255); g = (uint8_t)(q * 255); b = (uint8_t)(v * 255); break;
        case 4: r = (uint8_t)(t * 255); g = (uint8_t)(p * 255); b = (uint8_t)(v * 255); break;
        default: r = (uint8_t)(v * 255); g = (uint8_t)(p * 255); b = (uint8_t)(q * 255); break;
    }
    
    // Set all LEDs to the same color (or create a sweep effect)
    // Calculate active LEDs - use ceil to ensure proper rounding up
    // When progress is very close to 1.0, ensure all LEDs light up
    uint32_t active_leds;
    if (progress >= 1.0f) {
        active_leds = CONFIG_LED_AUDIO_LED_COUNT;
    } else {
        active_leds = (uint32_t)ceilf(progress * CONFIG_LED_AUDIO_LED_COUNT);
        if (active_leds > CONFIG_LED_AUDIO_LED_COUNT) {
            active_leds = CONFIG_LED_AUDIO_LED_COUNT;
        }
    }
    // Ensure at least one LED is active if progress > 0
    if (progress > 0.0f && active_leds == 0) {
        active_leds = 1;
    }
    for (uint32_t i = 0; i < CONFIG_LED_AUDIO_LED_COUNT; i++) {
        if (i < active_leds) {
            set_pixel_rgb(i, r, g, b);
        } else {
            set_pixel_rgb(i, 0, 0, 0);
        }
    }
    
    led_strip_refresh(s_strip);
}

// Play log sweep from embedded WAV file
static void play_log_sweep_pcm(void)
{
    size_t wav_size = _binary_256kMeasSweep_0_to_20000__12_dBFS_48k_Float_LR_refL_wav_end - _binary_256kMeasSweep_0_to_20000__12_dBFS_48k_Float_LR_refL_wav_start;
    const uint8_t *wav_data = _binary_256kMeasSweep_0_to_20000__12_dBFS_48k_Float_LR_refL_wav_start;
    
    ESP_LOGI(TAG, "Playing embedded log sweep WAV file (%zu bytes)", wav_size);
    ESP_LOGI(TAG, "WAV file first 16 bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
             wav_data[0], wav_data[1], wav_data[2], wav_data[3],
             wav_data[4], wav_data[5], wav_data[6], wav_data[7],
             wav_data[8], wav_data[9], wav_data[10], wav_data[11],
             wav_data[12], wav_data[13], wav_data[14], wav_data[15]);
    
    // Verify it's a valid WAV file (RIFF header)
    if (wav_size < 12 || wav_data[0] != 'R' || wav_data[1] != 'I' || 
        wav_data[2] != 'F' || wav_data[3] != 'F') {
        ESP_LOGE(TAG, "Invalid WAV file - missing RIFF header!");
        update_leds_for_audio(0.0f, false);
        return;
    }
    
    // Play the embedded WAV file with LED progress callback
    // The callback will update LEDs in sync with audio playback
    esp_err_t err = audio_player_play_wav(wav_data, 
                                          wav_size,
                                          update_leds_for_audio);  // Pass LED update function as callback
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to play WAV file: %s", esp_err_to_name(err));
        update_leds_for_audio(0.0f, false);  // Turn off LEDs on error
        return;
    }
    
    ESP_LOGI(TAG, "Log sweep WAV playback complete");
}

// Play MP3 file (if embedded or available)
static void play_mp3_file(const uint8_t *mp3_data, size_t mp3_len)
{
    if (!mp3_data || mp3_len == 0) {
        ESP_LOGW(TAG, "No MP3 data provided");
        return;
    }
    
    ESP_LOGI(TAG, "Playing MP3 file (%zu bytes)", mp3_len);
    
    mp3_decoder_t *decoder = mp3_decoder_create();
    if (!decoder) {
        ESP_LOGE(TAG, "Failed to create MP3 decoder");
        return;
    }
    
    const size_t pcm_buffer_size = 1152 * 2; // MP3 frame size * 2 for safety
    int16_t *pcm_buffer = malloc(pcm_buffer_size * sizeof(int16_t));
    if (!pcm_buffer) {
        ESP_LOGE(TAG, "Failed to allocate PCM buffer");
        mp3_decoder_destroy(decoder);
        return;
    }
    
    const uint8_t *mp3_ptr = mp3_data;
    size_t mp3_remaining = mp3_len;
    int sample_rate = 0;
    int channels = 0;
    size_t total_samples_played = 0;
    size_t total_duration_samples = 0;
    
    while (mp3_remaining > 0) {
        size_t samples_decoded = 0;
        int frame_sample_rate = 0;
        int frame_channels = 0;
        size_t bytes_consumed = 0;
        
        esp_err_t err = mp3_decoder_decode(decoder,
                                          mp3_ptr,
                                          mp3_remaining,
                                          pcm_buffer,
                                          pcm_buffer_size,
                                          &samples_decoded,
                                          &frame_sample_rate,
                                          &frame_channels,
                                          &bytes_consumed);
        
        if (err != ESP_OK || samples_decoded == 0) {
            if (bytes_consumed == 0) {
                // Need more data or end of stream
                break;
            }
        }
        
        if (samples_decoded > 0) {
            if (sample_rate == 0) {
                sample_rate = frame_sample_rate;
                channels = frame_channels;
                ESP_LOGI(TAG, "MP3: %d Hz, %d channel(s)", sample_rate, channels);
            }
            
            // Play decoded PCM
            err = audio_player_submit_pcm(pcm_buffer, samples_decoded / channels,
                                          sample_rate, channels);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to submit PCM: %s", esp_err_to_name(err));
            }
            
            total_samples_played += samples_decoded / channels;
            
            // Update LED progress (estimate)
            if (total_duration_samples == 0) {
                // Estimate total duration from first few frames
                total_duration_samples = (size_t)(sample_rate * 5.0f); // Assume 5 seconds
            }
            float progress = (float)total_samples_played / (float)total_duration_samples;
            if (progress > 1.0f) progress = 1.0f;
            update_leds_for_audio(progress, true);
        }
        
        if (bytes_consumed > 0) {
            mp3_ptr += bytes_consumed;
            mp3_remaining -= bytes_consumed;
        } else {
            break; // No progress, exit
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Final LED update
    update_leds_for_audio(1.0f, true);
    vTaskDelay(pdMS_TO_TICKS(100));
    update_leds_for_audio(0.0f, false);
    
    free(pcm_buffer);
    mp3_decoder_destroy(decoder);
    ESP_LOGI(TAG, "MP3 playback complete");
}

// Task for monitoring microphone input and making LEDs react to sound
static void audio_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "🎤 Audio monitor task started - LEDs will react to microphone input");

    // I2S1 configuration for microphone input on Korvo1
    // Korvo1 has ES8311 codec: DOUT (speaker) on I2S0, DIN (microphone) also on I2S0
    // For dual I/O on same I2S port: requires using I2S_CHANNEL_STEREO with separate bclk
    // Simplified: use I2S_NUM_1 for microphone with GPIO51 (DIN), GPIO40 (BCLK), GPIO41 (LRCK)

    const i2s_port_t i2s_port = I2S_NUM_1;
    i2s_config_t i2s_cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,  // Receive mode for microphone
        .sample_rate = 16000,  // 16kHz is typical for voice/mic
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  // Stereo
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
    };

    // Try to install I2S driver
    esp_err_t ret = i2s_driver_install(i2s_port, &i2s_cfg, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to install I2S driver for microphone: %s", esp_err_to_name(ret));
        // Don't fail completely - just disable audio monitoring
        vTaskDelete(NULL);
        return;
    }

    // Set I2S pin configuration - using GPIO51 for DIN if available, or skip
    // Actually: Korvo1 shares ES8311 on I2S0, might not have separate I2S1
    // For now just use I2S0 in dual mode or skip mic input
    // Let's just uninstall and skip
    i2s_driver_uninstall(i2s_port);
    ESP_LOGW(TAG, "Microphone I2S setup skipped - using visual feedback from existing audio");
    vTaskDelete(NULL);
}

void app_main(void)
{
    // Add current task to watchdog (ESP-IDF already initializes it)
    // Use default timeout - just make sure we're registered
    esp_task_wdt_add(NULL);
    
    ESP_LOGI(TAG, "Korvo1 LED and Audio Test");
    ESP_LOGI(TAG, "LEDs: %d pixels on GPIO %d (brightness=%d)",
             CONFIG_LED_AUDIO_LED_COUNT, CONFIG_LED_AUDIO_STRIP_GPIO, CONFIG_LED_AUDIO_BRIGHTNESS);
    ESP_LOGI(TAG, "Audio: %d Hz sample rate", CONFIG_AUDIO_SAMPLE_RATE);
    
    // Initialize NVS (required for some components)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Feed watchdog before SPIFFS init
    esp_task_wdt_reset();
    
    // Initialize SPIFFS for MP3 file storage (fallback if SD card not available)
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",  // Match partition name from partitions.csv
        .max_files = 10,
        .format_if_mount_failed = true  // Format if not formatted (first boot)
    };
    ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGW(TAG, "Failed to mount or format SPIFFS filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "SPIFFS partition 'storage' not found - MP3 files will use SD card if available");
        } else {
            ESP_LOGW(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
    } else {
        size_t total = 0, used = 0;
        ret = esp_spiffs_info("storage", &total, &used);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✅ SPIFFS mounted: %zu KB total, %zu KB used", total / 1024, used / 1024);
        }
    }
    
    // Initialize SD card for MP3 file storage (primary storage)
    // Try SDMMC mode first (1-bit mode, most compatible)
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    const char mount_point[] = "/sdcard";
    sdmmc_card_t *card = NULL;
    bool sd_card_mounted = false;
    
    // Feed watchdog before SD card init
    esp_task_wdt_reset();
    
    // Try SDMMC host (1-bit mode) - with shorter timeout to fail faster
    ESP_LOGI(TAG, "Initializing SD card (SDMMC mode)...");
    vTaskDelay(pdMS_TO_TICKS(50));  // Yield to prevent watchdog
    
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;  // Use 1-bit mode for compatibility
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    // Note: GPIO pins may need adjustment based on hardware
    // Common SDMMC pins: CLK=GPIO14, CMD=GPIO15, D0=GPIO2, D1=GPIO4, D2=GPIO12, D3=GPIO13
    // For 1-bit mode, only CLK, CMD, and D0 are needed
    
    // Try mount with timeout - if no card, fail quickly
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);
    if (ret == ESP_OK) {
        sd_card_mounted = true;
        sdmmc_card_print_info(stdout, card);
        ESP_LOGI(TAG, "✅ SD card mounted at %s", mount_point);
    } else {
        ESP_LOGW(TAG, "SDMMC mount failed: %s (no SD card detected)", esp_err_to_name(ret));
        // Skip SPI mode attempt - if SDMMC fails, card likely not present
        // This saves time and prevents watchdog
        ESP_LOGW(TAG, "SD card not available - MP3 files will use SPIFFS");
    }
    
    if (sd_card_mounted) {
        // Create sounds directory on SD card if it doesn't exist
        struct stat st = {0};
        char sounds_path[64];
        snprintf(sounds_path, sizeof(sounds_path), "%s/sounds", mount_point);
        if (stat(sounds_path, &st) == -1) {
            // Directory doesn't exist, try to create it
            // Note: mkdir may not be available, but we'll try
            ESP_LOGI(TAG, "SD card sounds directory will be created when files are copied");
        } else {
            ESP_LOGI(TAG, "SD card sounds directory exists: %s", sounds_path);
        }
    }
    
    // Initialize LED strip
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = CONFIG_LED_AUDIO_STRIP_GPIO,
        .max_leds = CONFIG_LED_AUDIO_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
        .flags = {
            .invert_out = false,
        },
    };
    
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .with_dma = false,
    };
    
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
    ESP_ERROR_CHECK(led_strip_clear(s_strip));
    ESP_LOGI(TAG, "LED strip initialized");
    esp_task_wdt_reset();  // Feed watchdog after LED strip init
    
    // Initialize LED indicators and pass the strip handle
    led_indicators_init();
    led_indicators_set_strip(s_strip);
    esp_task_wdt_reset();  // Feed watchdog after LED indicators init
    
    // Initialize action manager and pass the strip handle
    action_manager_init();
    action_manager_set_led_strip(s_strip);
    esp_task_wdt_reset();  // Feed watchdog after action manager init
    
    // Feed watchdog before audio player init (can take time with I2C operations)
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Initialize audio player (this does I2C operations which can be slow)
    esp_err_t audio_err = audio_player_init(&s_audio_config);
    // Feed watchdog after audio player init
    esp_task_wdt_reset();
    if (audio_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio player: %s", esp_err_to_name(audio_err));
        // Continue anyway - LEDs will still work
    } else {
        ESP_LOGI(TAG, "Audio player initialized");
    }
    
    // Feed watchdog before WiFi initialization
    esp_task_wdt_reset();
    
    // Initialize BLE for WiFi onboarding (before WiFi connection)
#ifdef CONFIG_BT_ENABLED
    esp_task_wdt_reset();  // Feed watchdog before BLE init
    somnus_ble_config_t ble_cfg = {
        .connect_cb = ble_wifi_connect_cb,
        .connect_ctx = NULL,
        .device_command_cb = ble_device_command_cb,
        .device_command_ctx = NULL
    };
    
    esp_err_t ble_err = somnus_ble_start(&ble_cfg);
    if (ble_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start BLE service: %s (continuing without BLE)", esp_err_to_name(ble_err));
    } else {
        ESP_LOGI(TAG, "✅ BLE service started - ready for WiFi onboarding");
        ESP_LOGI(TAG, "   Mobile app can scan WiFi and connect via BLE");
    }
    esp_task_wdt_reset();  // Feed watchdog after BLE init
#else
    ESP_LOGI(TAG, "BLE disabled in build configuration - skipping BLE initialization");
#endif
    
    // Initialize WiFi (required for Gemini API)
    esp_task_wdt_reset();  // Feed watchdog before WiFi init
    wifi_manager_init();
    
    // Configure WiFi from menuconfig
    #ifdef CONFIG_WIFI_SSID
    if (strlen(CONFIG_WIFI_SSID) > 0) {
        wifi_manager_config_t wifi_cfg = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD
        };
        ESP_LOGI(TAG, "Connecting to WiFi: %s", CONFIG_WIFI_SSID);
        esp_err_t wifi_err = wifi_manager_connect(&wifi_cfg);
        if (wifi_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to connect to WiFi: %s", esp_err_to_name(wifi_err));
            ESP_LOGE(TAG, "Voice assistant requires WiFi connection");
        } else {
            char ip_str[16];
            if (wifi_manager_get_ip(ip_str, sizeof(ip_str)) == ESP_OK) {
                ESP_LOGI(TAG, "WiFi connected, IP: %s", ip_str);
                
                // Initialize mDNS
                esp_err_t mdns_err = mdns_init();
                if (mdns_err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(mdns_err));
                } else {
                    // Set hostname
                    mdns_err = mdns_hostname_set("nap");
                    if (mdns_err != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(mdns_err));
                    } else {
                        ESP_LOGI(TAG, "mDNS hostname set to: nap.local");
                    }
                    
                    // Set default instance
                    mdns_err = mdns_instance_name_set("Korvo1 Voice Assistant");
                    if (mdns_err != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to set mDNS instance name: %s", esp_err_to_name(mdns_err));
                    }
                    
                    // Add HTTP service
                    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
                    ESP_LOGI(TAG, "✅ mDNS service advertised");
                }
                esp_task_wdt_reset();  // Feed watchdog after mDNS init
                
                // Start webserver after WiFi is connected
                webserver_config_t ws_cfg = {
                    .port = 80
                };
                esp_task_wdt_reset();  // Feed watchdog before webserver start
                esp_err_t ws_err = webserver_start(&s_webserver, &ws_cfg);
                if (ws_err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to start webserver: %s", esp_err_to_name(ws_err));
                } else {
                    ESP_LOGI(TAG, "✅ Webserver started - access dashboard at http://nap.local/ or http://%s/", ip_str);
                }
                
                // Initialize and start sensor integration (publishes to naptick API)
                esp_task_wdt_reset();  // Feed watchdog before sensor init
                esp_err_t sensor_err = sensor_integration_init();
                if (sensor_err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to initialize sensor integration: %s", esp_err_to_name(sensor_err));
                } else {
                    sensor_err = sensor_integration_start();
                    if (sensor_err != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to start sensor integration: %s", esp_err_to_name(sensor_err));
                    } else {
                        ESP_LOGI(TAG, "✅ Sensor integration started - publishing to naptick API");
                    }
                }
                esp_task_wdt_reset();  // Feed watchdog after sensor init
            }
        }
    } else {
        ESP_LOGW(TAG, "WiFi SSID not configured - set CONFIG_WIFI_SSID in menuconfig");
        ESP_LOGW(TAG, "WiFi is required for Gemini API - voice assistant will not work without it");
    }
    #endif
    
    // Initialize voice assistant (Gemini STT-LLM-TTS)
    #ifdef CONFIG_GEMINI_API_KEY
    if (strlen(CONFIG_GEMINI_API_KEY) > 0) {
        esp_task_wdt_reset();  // Feed watchdog before voice assistant init
        voice_assistant_config_t va_config = {
            .gemini_api_key = CONFIG_GEMINI_API_KEY,
            .gemini_model = CONFIG_GEMINI_MODEL
        };
        
        ESP_LOGI(TAG, "Initializing Gemini voice assistant (model: %s)...", CONFIG_GEMINI_MODEL);
        esp_err_t va_err = voice_assistant_init(&va_config);
        if (va_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize voice assistant: %s", esp_err_to_name(va_err));
        } else {
            ESP_LOGI(TAG, "✅ Voice assistant initialized (model: %s)", CONFIG_GEMINI_MODEL);
            esp_task_wdt_reset();  // Feed watchdog before voice assistant start
            va_err = voice_assistant_start();
            if (va_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start voice assistant: %s", esp_err_to_name(va_err));
            } else {
                ESP_LOGI(TAG, "✅ Voice assistant started - ready for wake word commands");
            }
        }
        esp_task_wdt_reset();  // Feed watchdog after voice assistant init
    } else {
        ESP_LOGW(TAG, "⚠️  Gemini API key not configured - voice assistant disabled");
        ESP_LOGW(TAG, "Set CONFIG_GEMINI_API_KEY in menuconfig or sdkconfig.defaults");
    }
    #else
    ESP_LOGW(TAG, "⚠️  Gemini API key configuration not available - rebuild with menuconfig");
    #endif
    
    // Initialize audio file manager (for MP3 playback)
    esp_task_wdt_reset();  // Feed watchdog before audio file manager init
    esp_err_t afm_err = audio_file_manager_init();
    if (afm_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize audio file manager: %s", esp_err_to_name(afm_err));
    } else {
        size_t track_count = audio_file_manager_get_count();
        ESP_LOGI(TAG, "✅ Audio file manager initialized with %zu tracks", track_count);
    }
    esp_task_wdt_reset();  // Feed watchdog after audio file manager init
    
    // Initialize wake word detection
    // Korvo1 uses I2S1 for microphone (ES7210 ADC) - separate from I2S0 (speaker/ES8311)
    esp_task_wdt_reset();  // Feed watchdog before wake word manager init
    esp_err_t wake_err = wake_word_manager_init();
    if (wake_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize wake word manager: %s", esp_err_to_name(wake_err));
        ESP_LOGW(TAG, "Wake word detection will not be available");
    } else {
        ESP_LOGI(TAG, "Wake word manager initialized");
        // Start wake word detection
        wake_err = wake_word_manager_start();
        if (wake_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start wake word detection: %s", esp_err_to_name(wake_err));
        } else {
            ESP_LOGI(TAG, "Wake word detection active - listening for wake words");
        }
    }
    
    // Startup animation: brief rainbow sweep
    ESP_LOGI(TAG, "Starting LED animation...");
    for (int i = 0; i < 360; i += 5) {
        float hue = (float)i / 360.0f;
        float s = 1.0f;
        float v = 0.5f;
        
        int hi = (int)(hue * 6.0f);
        float f = hue * 6.0f - hi;
        float p = v * (1.0f - s);
        float q = v * (1.0f - f * s);
        float t = v * (1.0f - (1.0f - f) * s);
        
        uint8_t r, g, b;
        switch (hi % 6) {
            case 0: r = (uint8_t)(v * 255); g = (uint8_t)(t * 255); b = (uint8_t)(p * 255); break;
            case 1: r = (uint8_t)(q * 255); g = (uint8_t)(v * 255); b = (uint8_t)(p * 255); break;
            case 2: r = (uint8_t)(p * 255); g = (uint8_t)(v * 255); b = (uint8_t)(t * 255); break;
            case 3: r = (uint8_t)(p * 255); g = (uint8_t)(q * 255); b = (uint8_t)(v * 255); break;
            case 4: r = (uint8_t)(t * 255); g = (uint8_t)(p * 255); b = (uint8_t)(v * 255); break;
            default: r = (uint8_t)(v * 255); g = (uint8_t)(p * 255); b = (uint8_t)(q * 255); break;
        }
        
        for (uint32_t j = 0; j < CONFIG_LED_AUDIO_LED_COUNT; j++) {
            set_pixel_rgb(j, r, g, b);
        }
        led_strip_refresh(s_strip);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    
    // Clear LEDs
    ESP_ERROR_CHECK(led_strip_clear(s_strip));
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Play offline welcome message first (simple audio test without WiFi)
    ESP_LOGI(TAG, "=== Playing offline welcome message ===");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Pause wake word detection during playback to prevent feedback loop
    // The microphone would pick up the speaker output and trigger false wake words
    wake_word_manager_pause();

    {
        const uint8_t *offline_wav = _binary_offline_welcome_wav_start;
        size_t offline_wav_size = _binary_offline_welcome_wav_end - _binary_offline_welcome_wav_start;

        ESP_LOGI(TAG, "Playing offline welcome message (%zu bytes)", offline_wav_size);

        // Verify WAV header
        if (offline_wav_size < 44 ||
            offline_wav[0] != 'R' || offline_wav[1] != 'I' ||
            offline_wav[2] != 'F' || offline_wav[3] != 'F') {
            ESP_LOGE(TAG, "Invalid offline WAV file - missing RIFF header!");
        } else {
            esp_err_t offline_err = audio_player_play_wav(offline_wav, offline_wav_size, update_leds_for_audio);
            if (offline_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to play offline welcome: %s", esp_err_to_name(offline_err));
            } else {
                ESP_LOGI(TAG, "✅ Offline welcome message playback complete");
            }
        }
    }
    
    // Resume wake word detection after playback completes
    wake_word_manager_resume();

    ESP_LOGI(TAG, "=== Offline welcome complete ===");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Play log sweep once as startup indicator
    // DISABLED: Skip automatic sweep playback
    // ESP_LOGI(TAG, "=== Playing log sweep test tone ===");
    // if (audio_err == ESP_OK) {
    //     play_log_sweep_pcm();
    // } else {
    //     ESP_LOGW(TAG, "Skipping audio (audio player not initialized)");
    //     // Just animate LEDs
    //     for (int i = 0; i < 100; i++) {
    //         float progress = (float)i / 100.0f;
    //         update_leds_for_audio(progress, true);
    //         vTaskDelay(pdMS_TO_TICKS(50));
    //     }
    //     update_leds_for_audio(0.0f, false);
    // }
    ESP_LOGI(TAG, "=== Entering voice assistant mode ===");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Test TTS with welcome message (graceful fallback if offline/no WiFi)
    #ifdef CONFIG_GEMINI_API_KEY
    if (strlen(CONFIG_GEMINI_API_KEY) > 0) {
        // Only try TTS if voice assistant is active (WiFi connected)
        if (!voice_assistant_is_active()) {
            ESP_LOGW(TAG, "⚠️  Voice assistant not active (WiFi or API key issue) - skipping TTS test");
        } else {
            ESP_LOGI(TAG, "Testing TTS with welcome message...");
            vTaskDelay(pdMS_TO_TICKS(2000)); // Wait a bit after sweeps

            // Try TTS but don't block LED effects if it fails
            // Reasons it might fail:
            // - No WiFi connection (not configured or not available)
            // - No internet (firewall, China, etc.)
            // - API key invalid or quota exceeded
            // - Network timeout
            esp_err_t tts_err = voice_assistant_test_tts("Connected to Google Gemini");
            if (tts_err == ESP_OK) {
                ESP_LOGI(TAG, "✅ TTS test successful - welcome message should be playing");
            } else {
                // Graceful degradation: continue with LED effects even if TTS unavailable
                ESP_LOGW(TAG, "⚠️  TTS unavailable: %s (continuing with LED effects)", esp_err_to_name(tts_err));
            }
        }
    }
    #endif

    // Main loop: voice assistant mode with continuous LED effects
    while (true) {
        // Voice assistant is running in background
        // Wake word detection will trigger voice commands
        // LED effects continue running regardless of TTS availability
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
