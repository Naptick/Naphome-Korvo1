/**
 * @file audio_file_manager.c
 * @brief Audio file manager for MP3 playback
 */

#include "audio_file_manager.h"
#include "audio_player.h"
#include "mp3_decoder.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>

static const char *TAG = "audio_file_mgr";

// Maximum number of audio files
#define MAX_AUDIO_FILES 200

// Audio file registry
typedef struct {
    char name[128];           // File name without extension
    char display_name[128];   // Display name for UI
    char file_path[256];      // Full file path
    size_t file_size;         // File size in bytes
    bool available;           // Whether file is available
} audio_file_entry_t;

static audio_file_entry_t s_audio_files[MAX_AUDIO_FILES];
static size_t s_file_count = 0;
static bool s_initialized = false;
static bool s_playing = false;
static char s_current_playing[128] = {0};

// MP3 playback task
static TaskHandle_t s_playback_task = NULL;

// Playback parameters
typedef struct {
    char *file_path;
    int duration_seconds;  // -1 for full file
} playback_params_t;

// Forward declarations
static void mp3_playback_task(void *arg);
static esp_err_t load_mp3_file_list(void);
static const char *get_display_name(const char *filename);

esp_err_t audio_file_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing audio file manager...");
    
    memset(s_audio_files, 0, sizeof(s_audio_files));
    s_file_count = 0;
    s_playing = false;
    memset(s_current_playing, 0, sizeof(s_current_playing));

    // Load MP3 file list
    esp_err_t ret = load_mp3_file_list();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load MP3 file list: %s", esp_err_to_name(ret));
        // Continue anyway - files might be added later
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Audio file manager initialized with %zu files", s_file_count);
    return ESP_OK;
}

// Static list of MP3 files (163 total from sounds directory)
static const char* s_mp3_file_names[] = {
    "10min_meditation_anxiety", "10min_meditation_mindfulness", "10min_meditation_selflove",
    "10min_meditation_selfsoothing", "10min_meditation_sleep", "10min_meditation_stress",
    "10mins_meditation_reset", "15min_meditation_selflove", "5min_meditation_anxiety",
    "8min_meditation_anxiety", "affirmations_overthinking", "affirmations_selfconfidence",
    "affirmations_selfconfidence2", "affirmations_selfconfidence3", "affirmations_selflove1",
    "affirmations_selflove2", "assistant_speech", "assistant-speech", "brownnoise_presleep",
    "buddhas_path_presleep", "chants_crownchakra", "chants_heartchakra", "chants_rootchakra",
    "chants_sacralchakra", "chants_solarplexus", "chants_thirdeyechakra", "chants_throatchakra",
    "clear_horizon_presleep", "clear_horizon_sleep", "clear_horizon_wakeup", "clockwork_presleep",
    "clockwork_sleep", "clockwork_wakeup", "deep_harmony_presleep", "deep_harmony_sleep",
    "deep_harmony_wakeup", "deep-in-the-ocean-116172", "dusk_serenity_presleep", "dusk_serenity_sleep",
    "dusk_serenity_wakeup", "fan_whisper_presleep", "fan_whisper_sleep", "fan_whisper_wakeup",
    "forest_waterfall_presleep", "forest_waterfall_sleep", "forest_waterfall_wakeup",
    "frequencies_fear1", "frequencies_healing1", "frequencies_stress", "gentle water",
    "gentle_relaxation_presleep", "gentle_spirit_presleep", "gentle_spirit_sleep", "gentle_spirit_wakeup",
    "healing_calmness_presleep", "hearth_glow_presleep", "house_lo", "inner_stillness_presleep",
    "inner_stillness_sleep", "inner_stillness_wakeup", "isochronic_presleep", "light instrumental",
    "light-rain-ambient-114354", "meditation_anxiety", "meditations_easeworry", "meditations_fear",
    "meditations_negativethoughts", "meditations_stress", "mindfulness1", "mindfulness2",
    "moonlit_solitude_presleep", "moonlit_solitude_sleep", "moonlit_solitude_wakeup",
    "mountain_mist_sleep", "mountain_mist_wakeup", "mountian_mist_presleep",
    "negative_energy_release_1", "negative_energy_release_2", "night_murmur_presleep",
    "night_murmur_sleep", "night_murmur_wakeup", "noise_white", "ocean_embrace_presleep",
    "ocean_embrace_sleep", "ocean_embrace_wakeup", "ocean-waves-sea-beach-close-stereo-25857",
    "paris_rain_presleep", "paris_rain_sleep", "paris_rain_wakeup", "pink_noise_presleep",
    "pink-noise-distortion-90884", "presleep_ambient_city_sounds", "presleep_beachside_shack",
    "presleep_clock_ticking", "presleep_engine_seatbelt", "presleep_footsteps_whispers",
    "presleep_pages_whispers", "presleep_rain_against_window", "presleep_river_flowing",
    "presleep_sound_bowls_rain", "presleep_train_sound", "pure_hush_presleep", "pure_hush_sleep",
    "pure_hush_wakeup", "raag_bhoopali_presleep", "raag_hamsa_presleep", "raag_yaman_presleep",
    "sacred_renewa_presleep", "sacred_renewa_sleep", "sacred_renewa_wakeup",
    "seaside_whisper_presleep", "seaside_whisper_sleep", "seaside_whisper_wakeup",
    "singingbowls_presleep", "sky_cabin_presleep", "sky_cabin_sleep", "sky_cabin_wakeup",
    "sleep_ac_hum", "sleep_calm_river", "sleep_crickets_waves", "sleep_distant_city_sounds",
    "sleep_gentle_rain_sound", "sleep_inside_a_car", "sleep_rotating_fan", "sleep_rustling_leaves",
    "sleep_soft_classical", "sleep_windchimes_rain", "soft-piano-music-255000", "speech",
    "stories_akinosuke", "stories_atlantis", "stories_cafe", "stories_cityside",
    "stories_deepsleepermountain_presleep", "stories_dreamworld", "stories_jupiter_presleep",
    "stories_lavender_presleep", "stories_midnightlaundry_presleep", "stories_nighttrain_presleep",
    "stories_ocean", "stories_travelsanddreams", "tts_response", "twilight_haze_presleep",
    "twilight_haze_sleep", "twilight_haze_wakeup", "urba_ rain_sleep", "urban_rain_presleep",
    "urban_rain_wakeup", "wakeup_calm_waves_birds", "wakeup_gentle_wake_up", "wakeup_loud_flowing_water",
    "wakeup_meditation_sound", "wakeup_radio_sound", "wakeup_rain_and_puddles", "wakeup_soft_classical",
    "wakeup_soft_jazz", "wakeup_upbeat_classical", "wakeup_upbeat_instrumental",
    "walking_forest_presleep", "white_noise", "woodland_calm_presleep", "woodland_calm_sleep",
    "woodland_calm_wakeup"
};
#define MP3_FILE_COUNT (sizeof(s_mp3_file_names) / sizeof(s_mp3_file_names[0]))

static esp_err_t load_mp3_file_list(void)
{
    // Try SD card first (primary storage for MP3 files)
    const char *sounds_dir = "/sdcard/sounds";
    DIR *dir = opendir(sounds_dir);
    
    // Fallback to SD card root
    if (!dir) {
        sounds_dir = "/sdcard";
        dir = opendir(sounds_dir);
    }
    
    // Fallback to SPIFFS
    if (!dir) {
        sounds_dir = "/spiffs/sounds";
        dir = opendir(sounds_dir);
    }
    
    // Fallback to generic sounds directory
    if (!dir) {
        sounds_dir = "/sounds";
        dir = opendir(sounds_dir);
    }
    
    bool use_static_list = false;
    if (!dir) {
        ESP_LOGW(TAG, "Sounds directory not found, using static file list (%d files)", MP3_FILE_COUNT);
        use_static_list = true;
    }

    if (use_static_list) {
        // Use static list of known files - they may be in SPIFFS or SD card
        // Feed watchdog periodically during file scanning
        for (size_t i = 0; i < MP3_FILE_COUNT && s_file_count < MAX_AUDIO_FILES; i++) {
            // Feed watchdog every 10 files to prevent timeout during scanning
            if (i % 10 == 0) {
                esp_task_wdt_reset();
            }
            
            audio_file_entry_t *file = &s_audio_files[s_file_count];
            const char *name = s_mp3_file_names[i];
            
            strncpy(file->name, name, sizeof(file->name) - 1);
            file->name[sizeof(file->name) - 1] = '\0';
            
            // Get display name
            const char *display = get_display_name(file->name);
            strncpy(file->display_name, display, sizeof(file->display_name) - 1);
            file->display_name[sizeof(file->display_name) - 1] = '\0';
            
            // Try to find file in multiple locations (SD card first, then SPIFFS)
            const char *search_paths[] = {
                "/sdcard/sounds/%s.mp3",      // SD card sounds directory (preferred)
                "/sdcard/%s.mp3",             // SD card root
                "/spiffs/sounds/%s.mp3",      // SPIFFS sounds directory
                "/spiffs/%s.mp3",             // SPIFFS root
                "/sounds/%s.mp3",            // Generic sounds directory
                NULL
            };
            
            file->available = false;
            file->file_size = 0;
            
            // Use temporary buffer to avoid restrict warning (file->name is in same struct)
            char temp_name[128];
            strncpy(temp_name, file->name, sizeof(temp_name) - 1);
            temp_name[sizeof(temp_name) - 1] = '\0';
            
            // Set default path (most likely location) - DON'T check existence during init
            // File availability will be checked lazily when the file is actually requested
            // This speeds up initialization significantly (no stat() calls on slow SD card)
            snprintf(file->file_path, sizeof(file->file_path), "/sdcard/sounds/%s.mp3", temp_name);
            file->available = false;  // Will be checked when file is requested
            file->file_size = 0;
            
            s_file_count++;
        }
        size_t available_count = 0;
        for (size_t i = 0; i < s_file_count; i++) {
            if (s_audio_files[i].available) available_count++;
        }
        ESP_LOGI(TAG, "Loaded %zu files from static list (%zu available)", s_file_count, available_count);
        return ESP_OK;
    }

    // Scan directory for MP3 files
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && s_file_count < MAX_AUDIO_FILES) {
        if (entry->d_type == DT_REG) {
            const char *name = entry->d_name;
            size_t name_len = strlen(name);
            
            // Check if it's an MP3 file
            if (name_len > 4 && strcasecmp(name + name_len - 4, ".mp3") == 0) {
                audio_file_entry_t *file = &s_audio_files[s_file_count];
                
                // Store name without extension
                strncpy(file->name, name, name_len - 4);
                file->name[name_len - 4] = '\0';
                
                // Get display name
                const char *display = get_display_name(file->name);
                strncpy(file->display_name, display, sizeof(file->display_name) - 1);
                file->display_name[sizeof(file->display_name) - 1] = '\0';
                
                // Build file path (use temporary buffer to avoid restrict warning)
                char temp_name[128];
                strncpy(temp_name, file->name, sizeof(temp_name) - 1);
                temp_name[sizeof(temp_name) - 1] = '\0';
                char temp_path[256];
                snprintf(temp_path, sizeof(temp_path), "%s/%s.mp3", sounds_dir, temp_name);
                strncpy(file->file_path, temp_path, sizeof(file->file_path) - 1);
                file->file_path[sizeof(file->file_path) - 1] = '\0';
                
                // Mark as available (we found it in the directory, so it exists)
                // Skip stat() call during init to speed up initialization - we'll get file size when needed
                file->available = true;
                file->file_size = 0;  // Will be populated lazily when needed
                
                s_file_count++;
                
                // Feed watchdog every 20 files to prevent timeout during scanning
                if (s_file_count % 20 == 0) {
                    esp_task_wdt_reset();
                }
            }
        }
    }
    
    closedir(dir);
    return ESP_OK;
}

// Convert filename to display name (remove underscores, capitalize, etc.)
static const char *get_display_name(const char *filename)
{
    static char display[128];
    size_t len = strlen(filename);
    size_t j = 0;
    bool capitalize_next = true;
    
    for (size_t i = 0; i < len && j < sizeof(display) - 1; i++) {
        char c = filename[i];
        
        if (c == '_') {
            display[j++] = ' ';
            capitalize_next = true;
        } else if (c >= 'a' && c <= 'z' && capitalize_next) {
            display[j++] = c - 32; // Uppercase
            capitalize_next = false;
        } else {
            display[j++] = c;
            capitalize_next = false;
        }
    }
    
    display[j] = '\0';
    return display;
}

size_t audio_file_manager_get_count(void)
{
    return s_file_count;
}

esp_err_t audio_file_manager_get_by_index(size_t index, audio_file_info_t *info)
{
    if (!info || index >= s_file_count) {
        return ESP_ERR_INVALID_ARG;
    }

    audio_file_entry_t *file = &s_audio_files[index];
    info->name = file->name;
    info->display_name = file->display_name;
    info->data = NULL;  // Files are loaded on demand
    info->data_len = file->file_size;
    
    return ESP_OK;
}

esp_err_t audio_file_manager_get_by_name(const char *name, audio_file_info_t *info)
{
    if (!name || !info) {
        return ESP_ERR_INVALID_ARG;
    }

    // Remove .mp3 extension if present
    char clean_name[128];
    strncpy(clean_name, name, sizeof(clean_name) - 1);
    size_t len = strlen(clean_name);
    if (len > 4 && strcasecmp(clean_name + len - 4, ".mp3") == 0) {
        clean_name[len - 4] = '\0';
    }

    // First try exact match (case-insensitive)
    for (size_t i = 0; i < s_file_count; i++) {
        if (strcasecmp(s_audio_files[i].name, clean_name) == 0) {
            audio_file_entry_t *file = &s_audio_files[i];
            info->name = file->name;
            info->display_name = file->display_name;
            info->data = NULL;
            info->data_len = file->file_size;
            return ESP_OK;
        }
    }
    
    // If exact match fails, try fuzzy matching for 8.3 filenames
    // Check if the stored name starts with the search name (handles "CHANTS~1" matching "chants_thirdeyechakra")
    // Also check if search name contains key parts that might match
    char search_upper[128];
    strncpy(search_upper, clean_name, sizeof(search_upper) - 1);
    search_upper[sizeof(search_upper) - 1] = '\0';
    for (size_t i = 0; search_upper[i]; i++) {
        search_upper[i] = toupper(search_upper[i]);
    }
    
    // Extract key parts from search name (e.g., "chants_thirdeyechakra" -> "chants" and "thirdeye")
    char key_part1[32] = {0};
    char key_part2[32] = {0};
    const char *underscore = strchr(clean_name, '_');
    if (underscore) {
        size_t part1_len = underscore - clean_name;
        if (part1_len < sizeof(key_part1)) {
            strncpy(key_part1, clean_name, part1_len);
            key_part1[part1_len] = '\0';
            for (size_t i = 0; key_part1[i]; i++) {
                key_part1[i] = toupper(key_part1[i]);
            }
        }
        strncpy(key_part2, underscore + 1, sizeof(key_part2) - 1);
        key_part2[sizeof(key_part2) - 1] = '\0';
        for (size_t i = 0; key_part2[i]; i++) {
            key_part2[i] = toupper(key_part2[i]);
        }
    }
    
    for (size_t i = 0; i < s_file_count; i++) {
        char stored_upper[128];
        strncpy(stored_upper, s_audio_files[i].name, sizeof(stored_upper) - 1);
        stored_upper[sizeof(stored_upper) - 1] = '\0';
        for (size_t j = 0; stored_upper[j]; j++) {
            stored_upper[j] = toupper(stored_upper[j]);
        }
        
        // Check if stored name starts with search name (handles "CHANTS~1" for "chants_thirdeyechakra")
        if (strncmp(stored_upper, search_upper, strlen(search_upper)) == 0) {
            audio_file_entry_t *file = &s_audio_files[i];
            info->name = file->name;
            info->display_name = file->display_name;
            info->data = NULL;
            info->data_len = file->file_size;
            ESP_LOGW(TAG, "Fuzzy match: '%s' matched '%s'", clean_name, file->name);
            return ESP_OK;
        }
        
        // Check if stored name contains key parts (e.g., "CHANTS~1" contains "CHANTS")
        if (key_part1[0] && strstr(stored_upper, key_part1) != NULL) {
            // Also check if it might be the right file by checking for other key parts
            // For "chants_thirdeyechakra", look for files starting with "CHANTS" that might be it
            if (strncmp(stored_upper, key_part1, strlen(key_part1)) == 0) {
                audio_file_entry_t *file = &s_audio_files[i];
                info->name = file->name;
                info->display_name = file->display_name;
                info->data = NULL;
                info->data_len = file->file_size;
                ESP_LOGW(TAG, "Fuzzy match (key part): '%s' matched '%s'", clean_name, file->name);
                return ESP_OK;
            }
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

// MP3 playback task
static void mp3_playback_task(void *arg)
{
    playback_params_t *params = (playback_params_t *)arg;
    if (!params || !params->file_path) {
        if (params) {
            free(params->file_path);
            free(params);
        }
        vTaskDelete(NULL);
        return;
    }

    char *file_path = params->file_path;
    int duration_seconds = params->duration_seconds;
    
    ESP_LOGI(TAG, "Starting MP3 playback: %s (duration: %d seconds)", file_path, duration_seconds);

    // Track playback time for duration limiting
    int64_t start_time_us = esp_timer_get_time();
    int64_t duration_us = (duration_seconds > 0) ? (duration_seconds * 1000000LL) : INT64_MAX;

    // Open file
    ESP_LOGI(TAG, "Opening file: %s", file_path);
    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open file: %s (errno: %d)", file_path, errno);
        free(file_path);
        s_playing = false;
        s_playback_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "File opened successfully: %s", file_path);
    
    // Increase file buffer size for SD card reads (reduces system calls)
    // Use larger buffer to reduce SD card access frequency
    // Note: setvbuf buffer is automatically freed when file is closed
    const size_t file_buffer_size = 16384;  // 16KB file buffer
    uint8_t *file_buffer_ptr = malloc(file_buffer_size);
    if (file_buffer_ptr) {
        setvbuf(fp, (char *)file_buffer_ptr, _IOFBF, file_buffer_size);
        ESP_LOGI(TAG, "File buffer set to %zu bytes for SD card optimization", file_buffer_size);
    } else {
        ESP_LOGW(TAG, "Failed to allocate file buffer, using default (may cause SD card read issues)");
    }

    // Create MP3 decoder
    ESP_LOGI(TAG, "Creating MP3 decoder...");
    mp3_decoder_t *decoder = mp3_decoder_create();
    if (!decoder) {
        ESP_LOGE(TAG, "Failed to create MP3 decoder");
        fclose(fp);
        free(file_path);
        s_playing = false;
        s_playback_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "MP3 decoder created successfully");

    // Allocate buffers
    // Increase MP3 buffer size significantly for SD card playback
    // Larger buffer = fewer SD card reads = less chance of underruns
    const size_t mp3_buffer_size = 32768;  // 32KB buffer for SD card (was 8KB)
    const size_t pcm_buffer_size = 1152 * 2 * sizeof(int16_t);
    
    // MP3 buffer can be in PSRAM (only accessed from task, not from interrupt)
    uint8_t *mp3_buffer = heap_caps_malloc(mp3_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mp3_buffer) {
        mp3_buffer = malloc(mp3_buffer_size);
    }
    
    // PCM buffer MUST be in internal RAM (DMA-capable) for I2S
    // I2S DMA interrupt only accesses I2S driver's internal buffers, not this buffer
    // This buffer is only accessed in task context
    int16_t *pcm_buffer = heap_caps_malloc(pcm_buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!pcm_buffer) {
        // Fallback to regular malloc if DMA-capable allocation fails
        pcm_buffer = malloc(pcm_buffer_size);
        ESP_LOGW(TAG, "PCM buffer not in DMA-capable memory - may cause issues");
    }
    
    if (!mp3_buffer || !pcm_buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffers (mp3=%p, pcm=%p)", mp3_buffer, pcm_buffer);
        mp3_decoder_destroy(decoder);
        fclose(fp);
        free(file_path);
        if (mp3_buffer) free(mp3_buffer);
        if (pcm_buffer) free(pcm_buffer);
        s_playing = false;
        s_playback_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Buffers allocated: mp3=%p (%zu bytes), pcm=%p (%zu bytes)", 
             mp3_buffer, mp3_buffer_size, pcm_buffer, pcm_buffer_size);

    int sample_rate = 0;
    int channels = 0;
    size_t bytes_read = 0;
    bool eof = false;
    int frame_count = 0;

    // Pre-fill MP3 buffer before starting playback (critical for SD card)
    // This prevents buffer underruns during playback
    ESP_LOGI(TAG, "Pre-filling MP3 buffer from SD card...");
    size_t prefill_read = fread(mp3_buffer, 1, mp3_buffer_size, fp);
    if (prefill_read > 0) {
        bytes_read = prefill_read;
        ESP_LOGI(TAG, "Pre-filled %zu bytes into MP3 buffer", prefill_read);
    } else {
        ESP_LOGE(TAG, "Failed to pre-fill buffer - file may be empty or unreadable");
        eof = true;
    }

    ESP_LOGI(TAG, "Starting MP3 decode loop...");
    while (s_playing && !eof) {
        // Check duration limit
        if (duration_seconds > 0) {
            int64_t elapsed_us = esp_timer_get_time() - start_time_us;
            if (elapsed_us >= duration_us) {
                ESP_LOGI(TAG, "Duration limit reached (%d seconds), stopping playback", duration_seconds);
                break;
            }
        }
        // Read MP3 data from SD card
        // Pre-fill buffer aggressively to prevent underruns
        // Only read when buffer is less than 50% full to maintain headroom
        const size_t refill_threshold = mp3_buffer_size / 2;  // Refill when < 50% full
        if (bytes_read < refill_threshold) {
            size_t to_read = mp3_buffer_size - bytes_read;
            // Read larger chunks for SD card (file buffer handles smaller reads)
            // Read up to 8KB at a time for better SD card efficiency
            const size_t chunk_size = 8192;  // Read 8KB at a time (file buffer will handle smaller ops)
            size_t total_read = 0;
            int64_t read_start = esp_timer_get_time();
            
            while (total_read < to_read && !eof) {
                size_t this_read = (to_read - total_read > chunk_size) ? chunk_size : (to_read - total_read);
                size_t read = fread(mp3_buffer + bytes_read + total_read, 1, this_read, fp);
                if (read < this_read) {
                    eof = true;
                }
                total_read += read;
                
                // Yield periodically but less frequently (file buffer reduces system call overhead)
                if (total_read < to_read && !eof && (total_read % 4096 == 0)) {
                    vTaskDelay(pdMS_TO_TICKS(1));  // Yield every 4KB
                }
            }
            
            bytes_read += total_read;
            int64_t read_time = esp_timer_get_time() - read_start;
            if (read_time > 10000) {  // Log if read took > 10ms
                ESP_LOGW(TAG, "SD card read took %lld us for %zu bytes", read_time, total_read);
            }
        }

        if (bytes_read == 0) {
            break;
        }

        // Decode MP3 frame
        size_t samples_decoded = 0;
        int frame_sample_rate = 0;
        int frame_channels = 0;
        size_t bytes_consumed = 0;

        esp_err_t err = mp3_decoder_decode(decoder,
                                          mp3_buffer,
                                          bytes_read,
                                          pcm_buffer,
                                          pcm_buffer_size / sizeof(int16_t),
                                          &samples_decoded,
                                          &frame_sample_rate,
                                          &frame_channels,
                                          &bytes_consumed);

        if (err == ESP_OK && samples_decoded > 0 && bytes_consumed > 0) {
            frame_count++;
            if (sample_rate == 0) {
                sample_rate = frame_sample_rate;
                channels = frame_channels;
                ESP_LOGI(TAG, "MP3 decoded: %d Hz, %d channel(s), frame %d", sample_rate, channels, frame_count);
            }
            if (frame_count % 100 == 0) {
                ESP_LOGI(TAG, "MP3 playback progress: %d frames decoded", frame_count);
            }

            // Submit PCM to audio player (all processing happens in task context, not interrupt)
            // The audio player will copy data to I2S DMA buffers in task context
            err = audio_player_submit_pcm(pcm_buffer, samples_decoded / channels,
                                         sample_rate, channels);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to submit PCM: %s", esp_err_to_name(err));
            }

            // Move remaining data to start of buffer
            if (bytes_consumed < bytes_read) {
                memmove(mp3_buffer, mp3_buffer + bytes_consumed, bytes_read - bytes_consumed);
                bytes_read -= bytes_consumed;
            } else {
                bytes_read = 0;
            }
        } else if (bytes_consumed > 0) {
            // Skip invalid data
            memmove(mp3_buffer, mp3_buffer + bytes_consumed, bytes_read - bytes_consumed);
            bytes_read -= bytes_consumed;
        } else {
            // No progress, read more
            if (bytes_read >= mp3_buffer_size) {
                ESP_LOGW(TAG, "Decoder stuck, skipping data");
                bytes_read = 0;
            }
        }

        // Yield to allow I2S DMA and other tasks to run
        // This prevents SD card reads from blocking I2S DMA operations
        vTaskDelay(pdMS_TO_TICKS(5));  // Reduced from 10ms to be more responsive
    }

    // Cleanup
    free(mp3_buffer);
    free(pcm_buffer);
    mp3_decoder_destroy(decoder);
    
    // Close file (setvbuf buffer is automatically freed when file is closed)
    fclose(fp);
    
    free(params->file_path);
    free(params);

    ESP_LOGI(TAG, "MP3 playback complete");
    s_playing = false;
    s_playback_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t audio_file_manager_play(const char *name, float volume, int duration)
{
    (void)volume;  // Volume handled by audio_player

    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(audio_file_manager_init(), TAG, "init failed");
    }

    // Stop current playback
    audio_file_manager_stop();

    ESP_LOGI(TAG, "Looking for audio file: %s", name);
    
    // Find file
    audio_file_info_t info;
    esp_err_t ret = audio_file_manager_get_by_name(name, &info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio file not found: %s", name);
        ESP_LOGE(TAG, "Available files: %zu total", s_file_count);
        // Log files that might match (starting with same prefix)
        char search_prefix[32] = {0};
        const char *underscore = strchr(name, '_');
        if (underscore) {
            size_t prefix_len = underscore - name;
            if (prefix_len < sizeof(search_prefix)) {
                strncpy(search_prefix, name, prefix_len);
                search_prefix[prefix_len] = '\0';
            }
        } else {
            strncpy(search_prefix, name, sizeof(search_prefix) - 1);
        }
        ESP_LOGE(TAG, "Searching for files starting with '%s'...", search_prefix);
        size_t matches_found = 0;
        for (size_t i = 0; i < s_file_count && matches_found < 10; i++) {
            char stored_upper[128];
            strncpy(stored_upper, s_audio_files[i].name, sizeof(stored_upper) - 1);
            stored_upper[sizeof(stored_upper) - 1] = '\0';
            for (size_t j = 0; stored_upper[j]; j++) {
                stored_upper[j] = toupper(stored_upper[j]);
            }
            char prefix_upper[32];
            strncpy(prefix_upper, search_prefix, sizeof(prefix_upper) - 1);
            prefix_upper[sizeof(prefix_upper) - 1] = '\0';
            for (size_t j = 0; prefix_upper[j]; j++) {
                prefix_upper[j] = toupper(prefix_upper[j]);
            }
            if (strncmp(stored_upper, prefix_upper, strlen(prefix_upper)) == 0) {
                ESP_LOGE(TAG, "  [%zu] %s -> %s (available: %s, path: %s)", i, s_audio_files[i].name, 
                        s_audio_files[i].display_name,
                        s_audio_files[i].available ? "yes" : "no",
                        s_audio_files[i].file_path);
                matches_found++;
            }
        }
        if (matches_found == 0) {
            // Log first few files if no matches
            for (size_t i = 0; i < (s_file_count < 5 ? s_file_count : 5); i++) {
                ESP_LOGE(TAG, "  [%zu] %s -> %s (available: %s)", i, s_audio_files[i].name, 
                        s_audio_files[i].display_name,
                        s_audio_files[i].available ? "yes" : "no");
            }
        }
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Found file: %s -> %s", name, info.display_name);

    // Find file path - check multiple locations if needed
    char file_path[256] = {0};
    bool file_available = false;
    
    // First, try to find the file in our list
    for (size_t i = 0; i < s_file_count; i++) {
        if (strcasecmp(s_audio_files[i].name, info.name) == 0) {
            strncpy(file_path, s_audio_files[i].file_path, sizeof(file_path) - 1);
            file_path[sizeof(file_path) - 1] = '\0';
            ESP_LOGI(TAG, "File entry found: path=%s, available=%s, size=%zu", 
                    file_path, s_audio_files[i].available ? "yes" : "no", s_audio_files[i].file_size);
            break;
        }
    }

    if (file_path[0] == '\0') {
        ESP_LOGE(TAG, "File path not found for: %s", name);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Check if file actually exists (lazy check - we skipped this during init for speed)
    struct stat st;
    if (stat(file_path, &st) == 0) {
        file_available = true;
        ESP_LOGI(TAG, "File exists: %s (size: %zu bytes)", file_path, (size_t)st.st_size);
    } else {
        // Try alternative paths
        const char *search_paths[] = {
            "/sdcard/sounds/%s.mp3",
            "/sdcard/%s.mp3",
            "/spiffs/sounds/%s.mp3",
            "/spiffs/%s.mp3",
            NULL
        };
        
        char temp_name[128];
        strncpy(temp_name, info.name, sizeof(temp_name) - 1);
        temp_name[sizeof(temp_name) - 1] = '\0';
        
        for (int i = 0; search_paths[i] != NULL; i++) {
            char alt_path[256];
            snprintf(alt_path, sizeof(alt_path), search_paths[i], temp_name);
            if (stat(alt_path, &st) == 0) {
                strncpy(file_path, alt_path, sizeof(file_path) - 1);
                file_path[sizeof(file_path) - 1] = '\0';
                file_available = true;
                ESP_LOGI(TAG, "File found at alternative path: %s (size: %zu bytes)", file_path, (size_t)st.st_size);
                break;
            }
        }
    }
    
    if (!file_available) {
        ESP_LOGE(TAG, "File not found: %s", file_path);
        ESP_LOGE(TAG, "Please ensure the file exists on SD card");
        ESP_LOGE(TAG, "Tried: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }

    // Allocate playback parameters
    playback_params_t *params = malloc(sizeof(playback_params_t));
    if (!params) {
        return ESP_ERR_NO_MEM;
    }
    
    params->file_path = strdup(file_path);
    if (!params->file_path) {
        free(params);
        return ESP_ERR_NO_MEM;
    }
    
    params->duration_seconds = duration;

    strncpy(s_current_playing, info.name, sizeof(s_current_playing) - 1);
    s_playing = true;

    // Pin MP3 playback task to CPU 1 to isolate from WiFi/network tasks on CPU 0
    BaseType_t task_ret = xTaskCreatePinnedToCore(mp3_playback_task,
                                                 "mp3_playback",
                                                 8192,
                                                 params,
                                                 5,
                                                 &s_playback_task,
                                                 1);  // CPU 1 for audio processing
    if (task_ret != pdPASS) {
        free(params->file_path);
        free(params);
        s_playing = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Started playback: %s (duration: %d seconds)", info.display_name, duration);
    return ESP_OK;
}

esp_err_t audio_file_manager_stop(void)
{
    if (s_playing && s_playback_task) {
        s_playing = false;
        // Wait for task to finish (with timeout)
        int timeout = 100; // 10 seconds
        while (s_playback_task != NULL && timeout-- > 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    memset(s_current_playing, 0, sizeof(s_current_playing));
    return ESP_OK;
}

bool audio_file_manager_is_playing(void)
{
    return s_playing;
}

esp_err_t audio_file_manager_get_all_names(char ***names, size_t *count)
{
    if (!names || !count) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(audio_file_manager_init(), TAG, "init failed");
    }

    *count = s_file_count;
    char **name_array = calloc(s_file_count, sizeof(char *));
    if (!name_array) {
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < s_file_count; i++) {
        name_array[i] = strdup(s_audio_files[i].name);
        if (!name_array[i]) {
            // Free allocated strings on error
            for (size_t j = 0; j < i; j++) {
                free(name_array[j]);
            }
            free(name_array);
            return ESP_ERR_NO_MEM;
        }
    }

    *names = name_array;
    return ESP_OK;
}
