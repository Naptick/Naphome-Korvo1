#include "audio_player.h"
#include "audio_eq.h"

#include <inttypes.h>
#include <string.h>

#include "driver/i2c.h"
#include "driver/gpio.h"
// Compatibility: use old I2C API for ESP-IDF v4.4
#define i2c_master_bus_handle_t i2c_port_t
#define i2c_master_dev_handle_t i2c_cmd_handle_t
#include "esp_check.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "driver/i2s_std.h"
#else
#include "driver/i2s.h"
#endif

#define AUDIO_PLAYER_I2C_FREQ_HZ 100000
#define ES8311_ADDR_7BIT 0x18  // 7-bit I2C address (becomes 0x30 when shifted for 8-bit)

// ES8311 register definitions (from es8311_reg.h)
#define ES8311_RESET_REG00       0x00
#define ES8311_CLK_MANAGER_REG01 0x01
#define ES8311_CLK_MANAGER_REG02 0x02
#define ES8311_CLK_MANAGER_REG03 0x03
#define ES8311_CLK_MANAGER_REG04 0x04
#define ES8311_CLK_MANAGER_REG05 0x05
#define ES8311_CLK_MANAGER_REG06 0x06
#define ES8311_CLK_MANAGER_REG07 0x07
#define ES8311_CLK_MANAGER_REG08 0x08
#define ES8311_SDPIN_REG09       0x09
#define ES8311_SDPOUT_REG0A      0x0A
#define ES8311_SYSTEM_REG0B      0x0B
#define ES8311_SYSTEM_REG0C      0x0C
#define ES8311_SYSTEM_REG0D      0x0D
#define ES8311_SYSTEM_REG0E      0x0E
#define ES8311_SYSTEM_REG0F      0x0F
#define ES8311_SYSTEM_REG10      0x10
#define ES8311_SYSTEM_REG11      0x11
#define ES8311_SYSTEM_REG12      0x12
#define ES8311_SYSTEM_REG13      0x13
#define ES8311_SYSTEM_REG14      0x14
#define ES8311_ADC_REG15         0x15
#define ES8311_ADC_REG16         0x16
#define ES8311_ADC_REG17         0x17
#define ES8311_ADC_REG1B         0x1B
#define ES8311_ADC_REG1C         0x1C
#define ES8311_DAC_REG31         0x31
#define ES8311_DAC_REG32         0x32
#define ES8311_DAC_REG37         0x37
#define ES8311_GPIO_REG44        0x44
#define ES8311_GP_REG45          0x45

typedef struct {
    bool initialized;
    audio_player_config_t cfg;
    int current_sample_rate;
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t i2c_dev;
    audio_eq_t eq_left;   // EQ for left channel
    audio_eq_t eq_right;  // EQ for right channel
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    i2s_chan_handle_t tx_handle;  // I2S TX channel handle (ESP-IDF 5.x)
#endif
} audio_player_state_t;

static audio_player_state_t s_audio;
static const char *TAG = "audio_player";

static esp_err_t es8311_write_reg(uint8_t reg, uint8_t value)
{
    if (s_audio.i2c_bus == I2C_NUM_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_task_wdt_reset(); // Feed watchdog before I2C operation
    // Use ESP-IDF v4.4 I2C API
    // ES8311_ADDR_7BIT is 7-bit address, shift to get 8-bit write address
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ES8311_ADDR_7BIT << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(s_audio.i2c_bus, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    esp_task_wdt_reset(); // Feed watchdog after I2C operation
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 write failed reg=0x%02x val=0x%02x err=%s", reg, value, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t es8311_read_reg(uint8_t reg, uint8_t *value)
{
    if (s_audio.i2c_bus == I2C_NUM_MAX || value == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_task_wdt_reset(); // Feed watchdog before I2C operation
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ES8311_ADDR_7BIT << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ES8311_ADDR_7BIT << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, value, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(s_audio.i2c_bus, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    esp_task_wdt_reset(); // Feed watchdog after I2C operation
    return err;
}

static esp_err_t es8311_probe(void)
{
    // Try to read chip ID to verify device is present
    uint8_t chip_id1 = 0, chip_id2 = 0;
    esp_err_t err1 = es8311_read_reg(0xFD, &chip_id1);
    esp_err_t err2 = es8311_read_reg(0xFE, &chip_id2);
    if (err1 == ESP_OK && err2 == ESP_OK) {
        ESP_LOGI(TAG, "ES8311 detected at 0x%02x: Chip ID1=0x%02x ID2=0x%02x", ES8311_ADDR_7BIT, chip_id1, chip_id2);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "ES8311 probe failed at 0x%02x: err1=%s err2=%s", ES8311_ADDR_7BIT, esp_err_to_name(err1), esp_err_to_name(err2));
    return ESP_FAIL;
}

static esp_err_t es8311_config_clock_48000(void)
{
    // Configure clock for 48000 Hz when use_mclk=false (codec generates MCLK from BCLK)
    // BCLK = sample_rate * bits_per_sample * channels = 48000 * 16 * 2 = 1.536 MHz
    // MCLK will be generated internally from BCLK
    
    uint8_t regv;
    
    // CLK_MANAGER_REG02: pre_div=0 (means 1), pre_multi=3 (x8) when use_mclk=false
    ESP_RETURN_ON_ERROR(es8311_read_reg(ES8311_CLK_MANAGER_REG02, &regv), TAG, "read clk mgr 2");
    regv &= 0x07;  // Keep lower 3 bits
    regv |= (0 << 5);  // pre_div = 1 (register value 0)
    regv |= (3 << 3);  // pre_multi = x8 (register value 3) when use_mclk=false
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG02, regv), TAG, "clk mgr 2");
    
    // CLK_MANAGER_REG05: adc_div=0 (means 1), dac_div=0 (means 1)
    regv = 0x00;
    regv |= (0 << 4);  // adc_div = 1 (register value 0)
    regv |= (0 << 0);  // dac_div = 1 (register value 0)
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG05, regv), TAG, "clk mgr 5");
    
    // CLK_MANAGER_REG03: fs_mode=0, adc_osr=0x10
    ESP_RETURN_ON_ERROR(es8311_read_reg(ES8311_CLK_MANAGER_REG03, &regv), TAG, "read clk mgr 3");
    regv &= 0x80;  // Keep bit 7
    regv |= (0 << 6);  // fs_mode = 0
    regv |= 0x10;  // adc_osr = 0x10
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG03, regv), TAG, "clk mgr 3");
    
    // CLK_MANAGER_REG04: dac_osr=0x10
    ESP_RETURN_ON_ERROR(es8311_read_reg(ES8311_CLK_MANAGER_REG04, &regv), TAG, "read clk mgr 4");
    regv &= 0x80;  // Keep bit 7
    regv |= 0x10;  // dac_osr = 0x10
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG04, regv), TAG, "clk mgr 4");
    
    // CLK_MANAGER_REG07: lrck_h=0
    ESP_RETURN_ON_ERROR(es8311_read_reg(ES8311_CLK_MANAGER_REG07, &regv), TAG, "read clk mgr 7");
    regv &= 0xC0;  // Keep upper 2 bits
    regv |= 0x00;  // lrck_h = 0
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG07, regv), TAG, "clk mgr 7");
    
    // CLK_MANAGER_REG08: lrck_l=0xff
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG08, 0xFF), TAG, "clk mgr 8");
    
    // CLK_MANAGER_REG06: bclk_div=4 (register value 3)
    ESP_RETURN_ON_ERROR(es8311_read_reg(ES8311_CLK_MANAGER_REG06, &regv), TAG, "read clk mgr 6");
    regv &= 0xE0;  // Keep upper 3 bits
    regv |= 0x03;  // bclk_div = 4 (register value 3)
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG06, regv), TAG, "clk mgr 6");
    
    return ESP_OK;
}

static esp_err_t es8311_config_clock_44100(void)
{
    // Configure clock for 44100 Hz when use_mclk=false (codec generates MCLK from BCLK)
    // When use_mclk=false, pre_multi should be 3 (x8) per es8311_config_sample logic
    // BCLK = sample_rate * bits_per_sample * channels = 44100 * 16 * 2 = 1.4112 MHz
    // MCLK will be generated internally from BCLK
    
    uint8_t regv;
    
    // CLK_MANAGER_REG02: pre_div=0 (means 1), pre_multi=3 (x8) when use_mclk=false
    ESP_RETURN_ON_ERROR(es8311_read_reg(ES8311_CLK_MANAGER_REG02, &regv), TAG, "read clk mgr 2");
    regv &= 0x07;  // Keep lower 3 bits
    regv |= (0 << 5);  // pre_div = 1 (register value 0)
    regv |= (3 << 3);  // pre_multi = x8 (register value 3) when use_mclk=false
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG02, regv), TAG, "clk mgr 2");
    
    // CLK_MANAGER_REG05: adc_div=0 (means 1), dac_div=0 (means 1)
    regv = 0x00;
    regv |= (0 << 4);  // adc_div = 1 (register value 0)
    regv |= (0 << 0);  // dac_div = 1 (register value 0)
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG05, regv), TAG, "clk mgr 5");
    
    // CLK_MANAGER_REG03: fs_mode=0, adc_osr=0x10
    ESP_RETURN_ON_ERROR(es8311_read_reg(ES8311_CLK_MANAGER_REG03, &regv), TAG, "read clk mgr 3");
    regv &= 0x80;  // Keep bit 7
    regv |= (0 << 6);  // fs_mode = 0
    regv |= 0x10;  // adc_osr = 0x10
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG03, regv), TAG, "clk mgr 3");
    
    // CLK_MANAGER_REG04: dac_osr=0x10
    ESP_RETURN_ON_ERROR(es8311_read_reg(ES8311_CLK_MANAGER_REG04, &regv), TAG, "read clk mgr 4");
    regv &= 0x80;  // Keep bit 7
    regv |= 0x10;  // dac_osr = 0x10
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG04, regv), TAG, "clk mgr 4");
    
    // CLK_MANAGER_REG07: lrck_h=0
    ESP_RETURN_ON_ERROR(es8311_read_reg(ES8311_CLK_MANAGER_REG07, &regv), TAG, "read clk mgr 7");
    regv &= 0xC0;  // Keep upper 2 bits
    regv |= 0x00;  // lrck_h = 0
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG07, regv), TAG, "clk mgr 7");
    
    // CLK_MANAGER_REG08: lrck_l=0xff
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG08, 0xFF), TAG, "clk mgr 8");
    
    // CLK_MANAGER_REG06: bclk_div=4 (register value 3)
    ESP_RETURN_ON_ERROR(es8311_read_reg(ES8311_CLK_MANAGER_REG06, &regv), TAG, "read clk mgr 6");
    regv &= 0xE0;  // Keep upper 3 bits
    regv |= 0x03;  // bclk_div = 4 (register value 3)
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG06, regv), TAG, "clk mgr 6");
    
    return ESP_OK;
}

static esp_err_t es8311_init(void)
{
    // Feed watchdog at start
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(10)); // Yield to watchdog feed task
    
    // Probe device first
    ESP_LOGI(TAG, "Probing ES8311 at I2C address 0x%02x (7-bit)...", ES8311_ADDR_7BIT);
    esp_err_t probe_err = es8311_probe();
    esp_task_wdt_reset(); // Feed watchdog after probe
    vTaskDelay(pdMS_TO_TICKS(10)); // Yield again
    if (probe_err != ESP_OK) {
        ESP_LOGW(TAG, "ES8311 probe failed, continuing anyway...");
    }
    
    // ES8311 initialization sequence for DAC mode (playback only)
    // Based on ESP codec dev library implementation
    
    // Initial setup (from es8311_open)
    ESP_LOGI(TAG, "ES8311: Writing initial registers...");
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_GPIO_REG44, 0x08), TAG, "gpio 44"); // Enhance I2C noise immunity
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(5)); // Yield between writes
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_GPIO_REG44, 0x08), TAG, "gpio 44"); // Second write for reliability
    esp_task_wdt_reset(); // Feed watchdog after first few writes
    vTaskDelay(pdMS_TO_TICKS(5)); // Yield
    
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG01, 0x30), TAG, "clk mgr 1 init");
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG02, 0x00), TAG, "clk mgr 2 init");
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG03, 0x10), TAG, "clk mgr 3 init");
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_ADC_REG16, 0x24), TAG, "adc 16 init");
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG04, 0x10), TAG, "clk mgr 4 init");
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG05, 0x00), TAG, "clk mgr 5 init");
    esp_task_wdt_reset(); // Feed watchdog after clock manager writes
    vTaskDelay(pdMS_TO_TICKS(10)); // Longer yield after group
    
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG0B, 0x00), TAG, "sys 0B init");
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG0C, 0x00), TAG, "sys 0C init");
    // SYSTEM_REG10: HPOUT control - disable headphone output
    // Bit 7 typically enables/disables HPOUT, lower bits are volume
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG10, 0x00), TAG, "sys 10 init"); // Disable HPOUT (bit 7=0)
    esp_task_wdt_reset(); // Feed watchdog after system register writes
    
    // Reset FIRST (this will clear all registers, so we'll set REG11 after reset)
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_RESET_REG00, 0x80), TAG, "reset"); // Reset, slave mode
    esp_task_wdt_reset(); // Feed watchdog before delay
    vTaskDelay(pdMS_TO_TICKS(20)); // Delay after reset to allow registers to stabilize
    esp_task_wdt_reset(); // Feed watchdog after delay
    
    // Clock manager: Don't use external MCLK (use_mclk=false), generate from BCLK, no invert, slave mode
    // When use_mclk=false, bit 7 should be set (0xBF = 0x3F | 0x80)
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_CLK_MANAGER_REG01, 0xBF), TAG, "clk mgr 1"); // No external MCLK, generate from BCLK
    
    // Configure clock for 48000 Hz (matching the embedded WAV file)
    ESP_RETURN_ON_ERROR(es8311_config_clock_48000(), TAG, "clock config");
    esp_task_wdt_reset(); // Feed watchdog after clock config
    ESP_LOGI(TAG, "ES8311 clock configured for 48000 Hz");
    
    // I2S interface configuration - I2S format, 16-bit
    // Read-modify-write to enable DAC interface (clear bit 6) - must be done after clock config
    uint8_t dac_iface, adc_iface;
    ESP_RETURN_ON_ERROR(es8311_read_reg(ES8311_SDPIN_REG09, &dac_iface), TAG, "read sdp in");
    ESP_RETURN_ON_ERROR(es8311_read_reg(ES8311_SDPOUT_REG0A, &adc_iface), TAG, "read sdp out");
    esp_task_wdt_reset(); // Feed watchdog after register reads
    dac_iface &= 0xBF;  // Clear bit 6 first
    adc_iface &= 0xBF;  // Clear bit 6 first
    // For DAC mode, clear bit 6 again (equivalent to &= ~(BITS(6)))
    dac_iface &= ~(0x40);  // Clear bit 6 to enable DAC interface for playback
    // Set I2S format: 0x0C = I2S format, 16-bit
    dac_iface |= 0x0C;  // I2S format, 16-bit
    adc_iface |= 0x0C;  // I2S format, 16-bit
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SDPIN_REG09, dac_iface), TAG, "sdp in"); // DAC I2S, 16-bit, enabled
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SDPOUT_REG0A, adc_iface), TAG, "sdp out"); // ADC I2S, 16-bit
    esp_task_wdt_reset(); // Feed watchdog after I2S config writes
    
    // System configuration (from es8311_start)
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_ADC_REG17, 0xBF), TAG, "adc 17");
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG0E, 0x02), TAG, "sys 0E");
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG12, 0x00), TAG, "sys 12"); // Enable DAC (0x00 = enable)
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG14, 0x1A), TAG, "sys 14"); // Analog PGA gain
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG0D, 0x01), TAG, "sys 0D"); // Power up
    esp_task_wdt_reset(); // Feed watchdog before delay
    vTaskDelay(pdMS_TO_TICKS(20)); // Delay after power up to allow codec to stabilize
    esp_task_wdt_reset(); // Feed watchdog after delay
    
    // SYSTEM_REG0F: Output path selection - MUST be set BEFORE REG11
    // According to ES8311 datasheet, REG0F controls output path:
    // Bit 0-1: HPOUT control, Bit 2-3: SPKOUT control
    // Use 0x0C to enable SPKOUT only (disable HPOUT to avoid routing issues)
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG0F, 0x00), TAG, "sys 0F disable all"); // Disable all first
    esp_task_wdt_reset(); // Feed watchdog before delay
    vTaskDelay(pdMS_TO_TICKS(10)); // Small delay
    esp_task_wdt_reset(); // Feed watchdog after delay
    
    // Enable SPKOUT only (0x0C = bits 2-3 set for SPKOUT)
    uint8_t reg0f_value = 0x0C;  // Enable SPKOUT, disable HPOUT
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG0F, reg0f_value), TAG, "sys 0F enable SPKOUT");
    esp_task_wdt_reset(); // Feed watchdog before delay
    vTaskDelay(pdMS_TO_TICKS(10)); // Delay after REG0F
    esp_task_wdt_reset(); // Feed watchdog after delay
    // Read back to verify
    uint8_t reg0f_readback = 0;
    if (es8311_read_reg(ES8311_SYSTEM_REG0F, &reg0f_readback) == ESP_OK) {
        ESP_LOGI(TAG, "REG0F written=0x%02x, readback=0x%02x", reg0f_value, reg0f_readback);
    }
    esp_task_wdt_reset(); // Feed watchdog after read
    
    // SYSTEM_REG11: SPKOUT control - enable speaker output
    // Bit 7 typically enables SPKOUT (1=enable), lower bits are volume
    // Write REG11 AFTER REG0F to ensure path is enabled first
    // Try 0x80 first (just enable bit), then set volume
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG11, 0x80), TAG, "sys 11 enable SPKOUT bit"); // Enable SPKOUT (bit 7=1), min volume
    esp_task_wdt_reset(); // Feed watchdog before delay
    vTaskDelay(pdMS_TO_TICKS(10)); // Delay after setting enable bit
    esp_task_wdt_reset(); // Feed watchdog after delay
    
    // Now set volume: 0xFF = max, 0xE0 = ~88% volume, 0xD0 = ~81% volume
    // Using 0xE0 for good volume without distortion
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG11, 0xE0), TAG, "sys 11 set SPKOUT volume"); // Enable SPKOUT (bit 7=1) + volume
    esp_task_wdt_reset(); // Feed watchdog before delay
    vTaskDelay(pdMS_TO_TICKS(10)); // Delay after setting volume
    esp_task_wdt_reset(); // Feed watchdog after delay
    // Read back to verify and retry if bit 7 is not set
    uint8_t reg11_readback = 0;
    int retry_count = 0;
    const int max_retries = 5;
    while (retry_count < max_retries) {
        esp_task_wdt_reset(); // Feed watchdog in retry loop
        if (es8311_read_reg(ES8311_SYSTEM_REG11, &reg11_readback) == ESP_OK) {
            ESP_LOGI(TAG, "REG11 readback attempt %d: 0x%02x", retry_count + 1, reg11_readback);
            if ((reg11_readback & 0x80) != 0) {
                ESP_LOGI(TAG, "✅ REG11 bit 7 is set (SPKOUT enabled)");
                break; // Success
            }
        }
        
        // Bit 7 is not set, try to force enable
        ESP_LOGW(TAG, "REG11 bit 7 is 0, attempting to force enable (attempt %d/%d)...", 
                 retry_count + 1, max_retries);
        
        // Write 0xFF (max volume, ensure enable bit is set)
        es8311_write_reg(ES8311_SYSTEM_REG11, 0xFF);
        esp_task_wdt_reset(); // Feed watchdog before delay
        vTaskDelay(pdMS_TO_TICKS(20));
        esp_task_wdt_reset(); // Feed watchdog after delay
        
        // Verify it was written
        uint8_t verify = 0;
        if (es8311_read_reg(ES8311_SYSTEM_REG11, &verify) == ESP_OK) {
            ESP_LOGI(TAG, "REG11 after writing 0xFF: 0x%02x", verify);
            esp_task_wdt_reset(); // Feed watchdog after read
            if ((verify & 0x80) != 0) {
                // Success! Now set to desired volume but keep enable bit
                es8311_write_reg(ES8311_SYSTEM_REG11, 0xE0);
                esp_task_wdt_reset(); // Feed watchdog before delay
                vTaskDelay(pdMS_TO_TICKS(10));
                esp_task_wdt_reset(); // Feed watchdog after delay
                if (es8311_read_reg(ES8311_SYSTEM_REG11, &reg11_readback) == ESP_OK) {
                    if ((reg11_readback & 0x80) != 0) {
                        ESP_LOGI(TAG, "✅ REG11 successfully set to 0x%02x (SPKOUT enabled)", reg11_readback);
                        break;
                    }
                }
            }
        }
        
        retry_count++;
        vTaskDelay(pdMS_TO_TICKS(50)); // Wait before retry
    }
    
    // Final check
    if (es8311_read_reg(ES8311_SYSTEM_REG11, &reg11_readback) == ESP_OK) {
        if ((reg11_readback & 0x80) == 0) {
            ESP_LOGE(TAG, "❌ CRITICAL: REG11 bit 7 still 0 after %d attempts! Speaker will not work!", max_retries);
            ESP_LOGE(TAG, "   This may indicate a hardware issue or register conflict");
            ESP_LOGE(TAG, "   REG11 readback: 0x%02x (expected bit 7=1)", reg11_readback);
            ESP_LOGE(TAG, "   Note: Audio may still work if REG0F enables SPKOUT path");
        } else {
            ESP_LOGI(TAG, "✅ REG11 final value: 0x%02x (SPKOUT enabled)", reg11_readback);
        }
    }
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_ADC_REG15, 0x40), TAG, "adc 15");
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_DAC_REG37, 0x08), TAG, "dac 37"); // Ramp rate
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_GP_REG45, 0x00), TAG, "gp 45");
    
    // DAC configuration
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_DAC_REG31, 0x00), TAG, "dac 31"); // Unmute
    // Volume: 0xFF = max, 0xE0 = ~-1dB, 0xD0 = ~-2dB, 0xC0 = ~0dB, 0xB0 = ~-3dB
    // Using 0xD0 for good volume without distortion
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_DAC_REG32, 0xD0), TAG, "dac 32"); // Good volume level
    
    // Additional registers from es8311_open
    // SYSTEM_REG13: May control output routing or bias
    // Try 0x30 or 0x00 to see if it affects output path
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG13, 0x30), TAG, "sys 13"); // Try different value for speaker routing
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_ADC_REG1B, 0x0A), TAG, "adc 1B");
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_ADC_REG1C, 0x6A), TAG, "adc 1C");
    
    // Enable power amplifier (GPIO38 on Korvo1)
    // This is critical for audio output!
    gpio_config_t pa_gpio_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_38),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pa_gpio_conf), TAG, "pa gpio config");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_38, 1), TAG, "pa enable"); // Set high to enable PA
    ESP_LOGI(TAG, "Power amplifier enabled on GPIO38");
    
    ESP_LOGI(TAG, "ES8311 initialized for 44100 Hz playback");
    
    // Explicitly enable/start the codec (equivalent to esp_codec_dev_open)
    // This ensures the codec is in the correct state for playback
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG0D, 0x01), TAG, "sys 0D enable"); // Ensure power up
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG12, 0x00), TAG, "sys 12 enable"); // Ensure DAC enabled
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG10, 0x00), TAG, "sys 10 disable HPOUT"); // Ensure HPOUT disabled
    
    // Re-verify and re-set REG0F and REG11 to ensure they're correct
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG0F, 0x0C), TAG, "sys 0F re-enable SPKOUT"); // Enable SPKOUT only
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Write REG11 with 0xFF (max) to ensure bit 7 is definitely set, then set to desired volume
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG11, 0xFF), TAG, "sys 11 max SPKOUT"); // Max volume to ensure enable
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_SYSTEM_REG11, 0xE0), TAG, "sys 11 final SPKOUT"); // Set to good volume level
    vTaskDelay(pdMS_TO_TICKS(10));
    
    ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_DAC_REG31, 0x00), TAG, "dac 31 unmute"); // Ensure unmuted
    vTaskDelay(pdMS_TO_TICKS(20)); // Final delay to ensure all registers are stable
    
    // Diagnostic: Read back key registers to verify configuration
    uint8_t reg10_val = 0, reg11_val = 0, reg0f_val = 0, reg12_val = 0;
    if (es8311_read_reg(ES8311_SYSTEM_REG10, &reg10_val) == ESP_OK &&
        es8311_read_reg(ES8311_SYSTEM_REG11, &reg11_val) == ESP_OK &&
        es8311_read_reg(ES8311_SYSTEM_REG0F, &reg0f_val) == ESP_OK &&
        es8311_read_reg(ES8311_SYSTEM_REG12, &reg12_val) == ESP_OK) {
        ESP_LOGI(TAG, "ES8311 output config: REG10=0x%02x (HPOUT), REG11=0x%02x (SPKOUT), REG0F=0x%02x (path), REG12=0x%02x (DAC)",
                 reg10_val, reg11_val, reg0f_val, reg12_val);
    }
    
    vTaskDelay(pdMS_TO_TICKS(50)); // Give codec time to stabilize
    
    ESP_LOGI(TAG, "ES8311 codec enabled and ready for playback");
    return ESP_OK;
}

static void scan_i2c_bus(void)
{
    ESP_LOGI(TAG, "Scanning I2C bus...");
    int devices_found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(s_audio.i2c_bus, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "I2C device found at address 0x%02X", addr);
            devices_found++;
        }
    }
    if (devices_found == 0) {
        ESP_LOGW(TAG, "No I2C devices found!");
    } else {
        ESP_LOGI(TAG, "Found %d I2C device(s)", devices_found);
    }
}

static esp_err_t configure_i2c(const audio_player_config_t *cfg)
{
    // Initialize I2C master bus on I2C_NUM_0 (Korvo1 uses I2C_NUM_0 for codec)
    // Use ESP-IDF v4.4 I2C API
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = cfg->i2c_sda_gpio,
        .scl_io_num = cfg->i2c_scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = AUDIO_PLAYER_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_NUM_0, &i2c_conf), TAG, "i2c param config");
    ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_NUM_0, i2c_conf.mode, 0, 0, 0), TAG, "i2c driver install");
    s_audio.i2c_bus = I2C_NUM_0;
    s_audio.i2c_dev = NULL; // Not used with v4.4 API
    
    // Give I2C bus time to stabilize
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Scan I2C bus to see what devices are present
    scan_i2c_bus();
    
    return ESP_OK;
}

static esp_err_t configure_i2s(const audio_player_config_t *cfg)
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // ESP-IDF 5.x: Use new I2S driver API
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(cfg->i2s_port, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;  // Auto clear the legacy data in the DMA buffer
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_audio.tx_handle, NULL), TAG, "create I2S TX channel failed");
    
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(cfg->default_sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = cfg->mclk_gpio,
            .bclk = cfg->bclk_gpio,
            .ws = cfg->lrclk_gpio,
            .dout = cfg->data_gpio,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_audio.tx_handle, &std_cfg), TAG, "init I2S std mode failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_audio.tx_handle), TAG, "enable I2S channel failed");
    ESP_LOGI(TAG, "I2S driver started on port %d (ESP-IDF 5.x)", cfg->i2s_port);
#else
    // ESP-IDF 4.x: Use legacy I2S driver API
    i2s_config_t i2s_conf = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = cfg->default_sample_rate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = 256,
        .use_apll = false,  // Don't use APLL when codec generates MCLK from BCLK
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,  // No fixed MCLK - codec generates it from BCLK (use_mclk=false)
    };

    i2s_pin_config_t pin_conf = {
        .mck_io_num = cfg->mclk_gpio,
        .bck_io_num = cfg->bclk_gpio,
        .ws_io_num = cfg->lrclk_gpio,
        .data_out_num = cfg->data_gpio,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };

    ESP_RETURN_ON_ERROR(i2s_driver_install(cfg->i2s_port, &i2s_conf, 0, NULL), TAG, "i2s install");
    ESP_RETURN_ON_ERROR(i2s_set_pin(cfg->i2s_port, &pin_conf), TAG, "i2s pins");
    ESP_RETURN_ON_ERROR(i2s_zero_dma_buffer(cfg->i2s_port), TAG, "i2s zero");
    ESP_RETURN_ON_ERROR(i2s_start(cfg->i2s_port), TAG, "i2s start"); // Start I2S driver
    ESP_LOGI(TAG, "I2S driver started on port %d (ESP-IDF 4.x)", cfg->i2s_port);
#endif
    return ESP_OK;
}

esp_err_t audio_player_init(const audio_player_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "cfg required");
    ESP_RETURN_ON_FALSE(cfg->bclk_gpio >= 0 && cfg->lrclk_gpio >= 0 && cfg->data_gpio >= 0,
                        ESP_ERR_INVALID_ARG, TAG, "invalid pins");

    if (s_audio.initialized) {
        return ESP_OK;
    }

    s_audio.cfg = *cfg;
    s_audio.current_sample_rate = cfg->default_sample_rate > 0 ? cfg->default_sample_rate : 44100;

    // Feed watchdog before I2C setup
    esp_task_wdt_reset();
    ESP_LOGI(TAG, "Configuring I2C...");
    ESP_RETURN_ON_ERROR(configure_i2c(cfg), TAG, "i2c setup");
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(50)); // Give I2C bus more time to stabilize
    esp_task_wdt_reset(); // Feed watchdog after delay
    ESP_LOGI(TAG, "I2C configured");
    
    esp_task_wdt_reset();
    ESP_LOGI(TAG, "Configuring I2S...");
    ESP_RETURN_ON_ERROR(configure_i2s(cfg), TAG, "i2s setup");
    esp_task_wdt_reset(); // Feed watchdog after I2S setup
    ESP_LOGI(TAG, "I2S configured");
    
    esp_task_wdt_reset();
    ESP_LOGI(TAG, "Initializing ES8311 codec...");
    ESP_RETURN_ON_ERROR(es8311_init(), TAG, "codec init");
    esp_task_wdt_reset(); // Feed watchdog after codec init
    ESP_LOGI(TAG, "ES8311 codec initialized");

    // Initialize EQ for both channels (enabled by default)
    // EQ will be re-initialized with actual sample rate when playback starts
    audio_eq_init(&s_audio.eq_left, s_audio.current_sample_rate, true);
    audio_eq_init(&s_audio.eq_right, s_audio.current_sample_rate, true);

    s_audio.initialized = true;
    ESP_LOGI(TAG, "Audio player ready (sr=%d)", s_audio.current_sample_rate);
    return ESP_OK;
}

static esp_err_t ensure_sample_rate(int sample_rate_hz)
{
    if (!s_audio.initialized || sample_rate_hz <= 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sample_rate_hz == s_audio.current_sample_rate) {
        return ESP_OK;
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // ESP-IDF 5.x: Disable channel, reconfigure clock, then re-enable
    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_audio.tx_handle), TAG, "disable channel for reconfig");
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz);
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_audio.tx_handle, &clk_cfg), TAG, "reconfig clock");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_audio.tx_handle), TAG, "re-enable channel after reconfig");
#else
    // ESP-IDF 4.x: Use legacy API
    ESP_RETURN_ON_ERROR(
        i2s_set_clk(s_audio.cfg.i2s_port,
                    sample_rate_hz,
                    I2S_BITS_PER_SAMPLE_16BIT,
                    I2S_CHANNEL_STEREO),
        TAG,
        "set clk");
#endif
    s_audio.current_sample_rate = sample_rate_hz;
    ESP_LOGI(TAG, "Playback sample rate -> %d Hz", sample_rate_hz);
    return ESP_OK;
}

static esp_err_t write_pcm_frames(const int16_t *samples, size_t sample_count, int num_channels)
{
    ESP_RETURN_ON_FALSE(samples && sample_count > 0, ESP_ERR_INVALID_ARG, TAG, "bad pcm args");
    ESP_RETURN_ON_FALSE(num_channels == 1 || num_channels == 2, ESP_ERR_INVALID_ARG, TAG, "channels");

    const size_t chunk_frames = 256;
    int16_t stereo_buffer[chunk_frames * 2];

    size_t frames_written = 0;
    static size_t total_frames_written = 0;  // Track total frames for diagnostics
    while (frames_written < sample_count) {
        size_t frames_this = chunk_frames;
        if (frames_this > sample_count - frames_written) {
            frames_this = sample_count - frames_written;
        }

        if (num_channels == 1) {
            for (size_t i = 0; i < frames_this; ++i) {
                int16_t sample = samples[frames_written + i];
                stereo_buffer[2 * i] = sample;
                stereo_buffer[2 * i + 1] = sample;
            }
        } else {
            memcpy(stereo_buffer,
                   &samples[(frames_written)*2],
                   frames_this * sizeof(int16_t) * 2);
        }

        size_t bytes_to_write = frames_this * sizeof(int16_t) * 2;
        size_t total_written = 0;
        
        // Log first chunk and every second to verify audio data
        static bool first_write_logged = false;
        static size_t write_count = 0;
        write_count++;

        if (!first_write_logged && frames_written == 0) {
            ESP_LOGI(TAG, "🔊 First I2S write: %zu frames, first 4 PCM samples: %d, %d, %d, %d",
                     frames_this, stereo_buffer[0], stereo_buffer[1], stereo_buffer[2], stereo_buffer[3]);
            first_write_logged = true;
        }

        // Log every 1000th write (~6 seconds at 16kHz) to verify continuous writing
        if (write_count % 1000 == 0) {
            ESP_LOGI(TAG, "🔊 I2S write #%zu: %zu frames, RMS level: %d",
                     write_count, frames_this, (int)((stereo_buffer[0] + stereo_buffer[2]) / 2));
        }
        
        while (total_written < bytes_to_write) {
            size_t bytes_written = 0;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
            // ESP-IDF 5.x: Use new I2S channel write API
            esp_err_t err = i2s_channel_write(s_audio.tx_handle,
                                               (uint8_t *)stereo_buffer + total_written,
                                               bytes_to_write - total_written,
                                               &bytes_written,
                                               portMAX_DELAY);
#else
            // ESP-IDF 4.x: Use legacy API
            esp_err_t err = i2s_write(s_audio.cfg.i2s_port,
                                      (uint8_t *)stereo_buffer + total_written,
                                      bytes_to_write - total_written,
                                      &bytes_written,
                                      portMAX_DELAY);
#endif
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(err));
                return err;
            }
            if (bytes_written == 0) {
                ESP_LOGW(TAG, "I2S write returned 0 bytes");
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }
            total_written += bytes_written;
        }
        frames_written += frames_this;
        total_frames_written += frames_this;
        
        // Log every 10000 frames (~0.23 seconds at 44.1kHz) to track playback
        if (total_frames_written % 10000 == 0) {
            ESP_LOGI(TAG, "Audio playback: %zu frames written to I2S", total_frames_written);
        }
    }
    return ESP_OK;
}

typedef struct __attribute__((packed)) {
    char chunk_id[4];
    uint32_t chunk_size;
    char format[4];
} wav_header_t;

typedef struct __attribute__((packed)) {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_fmt_body_t;

esp_err_t audio_player_play_wav(const uint8_t *wav_data, size_t wav_len, audio_progress_callback_t progress_cb)
{
    ESP_RETURN_ON_FALSE(s_audio.initialized, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_FALSE(wav_data && wav_len > sizeof(wav_header_t), ESP_ERR_INVALID_ARG, TAG, "bad wav");

    // Debug: log first few bytes to verify data
    ESP_LOGI(TAG, "=== WAV FILE VERIFICATION ===");
    ESP_LOGI(TAG, "WAV data: len=%zu bytes (%.2f MB)", wav_len, wav_len / (1024.0f * 1024.0f));
    ESP_LOGI(TAG, "First 32 bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
             wav_data[0], wav_data[1], wav_data[2], wav_data[3],
             wav_data[4], wav_data[5], wav_data[6], wav_data[7],
             wav_data[8], wav_data[9], wav_data[10], wav_data[11],
             wav_data[12], wav_data[13], wav_data[14], wav_data[15],
             wav_data[16], wav_data[17], wav_data[18], wav_data[19],
             wav_data[20], wav_data[21], wav_data[22], wav_data[23],
             wav_data[24], wav_data[25], wav_data[26], wav_data[27],
             wav_data[28], wav_data[29], wav_data[30], wav_data[31]);
    
    // Check RIFF header
    if (wav_len >= 4 && wav_data[0] == 'R' && wav_data[1] == 'I' && wav_data[2] == 'F' && wav_data[3] == 'F') {
        ESP_LOGI(TAG, "✓ Valid RIFF header");
    } else {
        ESP_LOGE(TAG, "✗ INVALID RIFF HEADER! First 4 bytes: %02x %02x %02x %02x",
                 wav_data[0], wav_data[1], wav_data[2], wav_data[3]);
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *ptr = wav_data;
    const uint8_t *end = wav_data + wav_len;
    const wav_header_t *hdr = (const wav_header_t *)ptr;
    if (memcmp(hdr->chunk_id, "RIFF", 4) != 0 || memcmp(hdr->format, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Invalid WAV header: chunk_id=%.4s, format=%.4s", hdr->chunk_id, hdr->format);
        return ESP_ERR_INVALID_ARG;
    }
    ptr += sizeof(wav_header_t);

    wav_fmt_body_t fmt = {0};
    bool fmt_found = false;
    const uint8_t *data_ptr = NULL;
    uint32_t data_size = 0;

    while (ptr + 8 <= end) {
        char chunk_id[4];
        memcpy(chunk_id, ptr, 4);
        uint32_t chunk_size = *(const uint32_t *)(ptr + 4);
        ptr += 8;
        if (ptr + chunk_size > end) {
            ESP_LOGE(TAG, "Chunk extends beyond end: chunk_size=%" PRIu32 ", remaining=%zu", chunk_size, end - ptr);
            return ESP_ERR_INVALID_ARG;
        }

        if (!fmt_found && memcmp(chunk_id, "fmt ", 4) == 0) {
            ESP_LOGI(TAG, "Found fmt chunk, size=%" PRIu32, chunk_size);
            if (chunk_size < 16) {
                ESP_LOGE(TAG, "fmt chunk too small: %" PRIu32 " < 16", chunk_size);
                return ESP_ERR_INVALID_ARG;
            }
            // Read fmt chunk - standard is 16 bytes, but we support up to sizeof(wav_fmt_body_t)
            memset(&fmt, 0, sizeof(wav_fmt_body_t));  // Zero out first
            size_t bytes_to_read = chunk_size < sizeof(wav_fmt_body_t) ? chunk_size : sizeof(wav_fmt_body_t);
            memcpy(&fmt, ptr, bytes_to_read);  // Copy available bytes
            fmt_found = true;
            ESP_LOGI(TAG, "WAV format: audio_format=%u, channels=%u, sample_rate=%" PRIu32 ", bits_per_sample=%u",
                     fmt.audio_format, fmt.num_channels, fmt.sample_rate, fmt.bits_per_sample);
            // Advance past the fmt chunk data (word-aligned)
            uint32_t advance = chunk_size;
            if (advance & 1) {
                advance++;  // WAV chunks are word-aligned
            }
            ptr += advance;
            continue;  // Continue to next chunk
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            ESP_LOGI(TAG, "Found data chunk, size=%" PRIu32, chunk_size);
            data_ptr = ptr;
            data_size = chunk_size;
            break;
        } else {
            ESP_LOGD(TAG, "Skipping chunk: %.4s, size=%" PRIu32, chunk_id, chunk_size);
            uint32_t advance = chunk_size;
            if (advance & 1) {
                advance++;
            }
            ptr += advance;
        }
    }

    if (!fmt_found) {
        ESP_LOGE(TAG, "WAV file missing fmt chunk");
        return ESP_ERR_INVALID_ARG;
    }
    if (!data_ptr) {
        ESP_LOGE(TAG, "WAV file missing data chunk (searched %zu bytes, ptr now at offset %zu from start)",
                 (size_t)(ptr - wav_data), (size_t)(ptr - wav_data));
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "WAV file parsed successfully: fmt found, data chunk found, data_size=%" PRIu32, data_size);
    
    // Support both PCM (format 1) and IEEE float (format 3)
    bool is_float = (fmt.audio_format == 3);
    bool is_pcm = (fmt.audio_format == 1);
    if (!is_pcm && !is_float) {
        ESP_LOGE(TAG, "Unsupported audio format: %u (expected 1=PCM or 3=float)", fmt.audio_format);
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    // For float, require 32-bit; for PCM, require 16-bit
    if (is_float) {
        if (fmt.bits_per_sample != 32) {
            ESP_LOGE(TAG, "Unsupported float bit depth: %u (expected 32)", fmt.bits_per_sample);
            return ESP_ERR_NOT_SUPPORTED;
        }
        ESP_LOGI(TAG, "Processing 32-bit float WAV, sample_rate=%" PRIu32 ", channels=%u", fmt.sample_rate, fmt.num_channels);
    } else {
        if (fmt.bits_per_sample != 16) {
            ESP_LOGE(TAG, "Unsupported PCM bit depth: %u (expected 16)", fmt.bits_per_sample);
            return ESP_ERR_NOT_SUPPORTED;
        }
        ESP_LOGI(TAG, "Processing 16-bit PCM WAV, sample_rate=%" PRIu32 ", channels=%u", fmt.sample_rate, fmt.num_channels);
    }

    ESP_LOGI(TAG, "Setting playback sample rate to %" PRIu32 " Hz", fmt.sample_rate);
    ESP_RETURN_ON_ERROR(ensure_sample_rate(fmt.sample_rate), TAG, "sr");
    ESP_LOGI(TAG, "I2S sample rate configured to %" PRIu32 " Hz", fmt.sample_rate);
    
    // Initialize/reset EQ with actual sample rate
    audio_eq_init(&s_audio.eq_left, fmt.sample_rate, true);
    audio_eq_init(&s_audio.eq_right, fmt.sample_rate, true);
    audio_eq_reset(&s_audio.eq_left);
    audio_eq_reset(&s_audio.eq_right);
    
    if (is_float) {
        // Convert 32-bit float to 16-bit PCM
        // Calculate frame count: data_size bytes / (bytes_per_sample * channels)
        size_t bytes_per_sample = fmt.bits_per_sample / 8;
        size_t bytes_per_frame = bytes_per_sample * fmt.num_channels;
        size_t frame_count = data_size / bytes_per_frame;
        const uint8_t *float_data = data_ptr;  // Keep as uint8_t to avoid direct flash access
        
        ESP_LOGI(TAG, "Float conversion: data_size=%u, bytes_per_frame=%u, frame_count=%u", 
                 (unsigned int)data_size, (unsigned int)bytes_per_frame, (unsigned int)frame_count);
        ESP_LOGI(TAG, "Expected duration: %.2f seconds at %" PRIu32 " Hz", 
                 (float)frame_count / (float)fmt.sample_rate, fmt.sample_rate);
        
        // Allocate buffers: one for float samples from flash (RAM copy), one for PCM output
        const size_t chunk_size = 1024;  // frames per chunk
        const size_t three_second_frame = (size_t)fmt.sample_rate * 3;  // Frame number at 3 seconds
        bool three_second_logged = false;
        bool signal_start_logged = false;  // Track when actual audio signal starts
        float *float_buffer = malloc(chunk_size * sizeof(float) * fmt.num_channels);
        int16_t *pcm_buffer = malloc(chunk_size * sizeof(int16_t) * fmt.num_channels);
        if (!float_buffer || !pcm_buffer) {
            ESP_LOGE(TAG, "Failed to allocate conversion buffers");
            free(float_buffer);
            free(pcm_buffer);
            return ESP_ERR_NO_MEM;
        }
        
        size_t frames_processed = 0;
        esp_err_t err = ESP_OK;
        
        while (frames_processed < frame_count && err == ESP_OK) {
            size_t frames_this_chunk = chunk_size;
            if (frames_processed + frames_this_chunk > frame_count) {
                frames_this_chunk = frame_count - frames_processed;
            }
            
            // Copy float samples from flash to RAM first (to avoid cache issues)
            // Calculate source offset in bytes (frames * bytes_per_frame)
            size_t byte_offset = frames_processed * bytes_per_frame;
            size_t float_bytes_this_chunk = frames_this_chunk * bytes_per_frame;
            const uint8_t *src = float_data + byte_offset;
            
            // Verify we're not reading past the data
            if (byte_offset + float_bytes_this_chunk > data_size) {
                ESP_LOGE(TAG, "Read would exceed data size: offset=%u, chunk=%u, total=%u", 
                         (unsigned int)byte_offset, (unsigned int)float_bytes_this_chunk, (unsigned int)data_size);
                err = ESP_ERR_INVALID_ARG;
                break;
            }
            
            // Copy from flash to RAM - memcpy should work if flash is properly mapped
            // If this fails, the embedded binary might not be accessible
            memcpy(float_buffer, src, float_bytes_this_chunk);
            
            // Process through EQ, then convert float to int16
            // Add headroom (90%) to prevent clipping and distortion
            static bool first_chunk_logged = false;
            float max_amp_this_chunk = 0.0f;
            const float headroom = 0.90f;  // 90% of full scale for headroom
            const float pcm_scale = 32767.0f * headroom;  // ~29490 for headroom
            
            // Process samples through EQ chain
            for (size_t i = 0; i < frames_this_chunk; i++) {
                float sample_f;
                float abs_sample;
                
                if (fmt.num_channels == 2) {
                    // Stereo: process left and right channels separately
                    float left = float_buffer[i * 2];
                    float right = float_buffer[i * 2 + 1];
                    
                    // Process through EQ
                    left = audio_eq_process(&s_audio.eq_left, 0, left);
                    right = audio_eq_process(&s_audio.eq_right, 1, right);
                    
                    // Clamp to [-1.0, 1.0] range
                    if (left > 1.0f) left = 1.0f;
                    if (left < -1.0f) left = -1.0f;
                    if (right > 1.0f) right = 1.0f;
                    if (right < -1.0f) right = -1.0f;
                    
                    // Track max amplitude
                    abs_sample = left < 0.0f ? -left : left;
                    if (abs_sample > max_amp_this_chunk) max_amp_this_chunk = abs_sample;
                    abs_sample = right < 0.0f ? -right : right;
                    if (abs_sample > max_amp_this_chunk) max_amp_this_chunk = abs_sample;
                    
                    // Convert to PCM
                    pcm_buffer[i * 2] = (int16_t)(left * pcm_scale);
                    pcm_buffer[i * 2 + 1] = (int16_t)(right * pcm_scale);
                    
                    // Log first few samples for verification
                    if (!first_chunk_logged && i < 4) {
                        ESP_LOGI(TAG, "First samples [%zu]: L=%.6f->%d, R=%.6f->%d", 
                                 i, float_buffer[i * 2], pcm_buffer[i * 2],
                                 float_buffer[i * 2 + 1], pcm_buffer[i * 2 + 1]);
                        if (i == 3) first_chunk_logged = true;
                    }
                } else {
                    // Mono: process single channel
                    sample_f = float_buffer[i];
                    
                    // Process through EQ (use left channel EQ for mono)
                    sample_f = audio_eq_process(&s_audio.eq_left, 0, sample_f);
                    
                    abs_sample = sample_f < 0.0f ? -sample_f : sample_f;
                    if (abs_sample > max_amp_this_chunk) {
                        max_amp_this_chunk = abs_sample;
                    }
                    
                    // Clamp to [-1.0, 1.0] range
                    if (sample_f > 1.0f) sample_f = 1.0f;
                    if (sample_f < -1.0f) sample_f = -1.0f;
                    
                    // Convert to PCM
                    pcm_buffer[i] = (int16_t)(sample_f * pcm_scale);
                    
                    // Log first few samples for verification
                    if (!first_chunk_logged && i < 8) {
                        ESP_LOGI(TAG, "First samples [%zu]: float=%.6f, PCM=%d", 
                                 i, sample_f, pcm_buffer[i]);
                        if (i == 7) first_chunk_logged = true;
                    }
                }
            }
            
            // Detect when actual audio signal starts (chirp begins)
            if (!signal_start_logged && max_amp_this_chunk > 0.001f) {
                float time_seconds = (float)frames_processed / (float)fmt.sample_rate;
                ESP_LOGI(TAG, "*** AUDIO SIGNAL DETECTED (chirp starts) at frame %zu (%.3f seconds), max_amp=%.6f ***",
                         frames_processed, time_seconds, max_amp_this_chunk);
                
                // Log sample values around the chirp start for verification
                ESP_LOGI(TAG, "Chirp samples (first 10 after detection):");
                for (size_t j = 0; j < 10 && j < frames_this_chunk * fmt.num_channels; j++) {
                    float sample_val = float_buffer[j];
                    int16_t pcm_val = pcm_buffer[j];
                    ESP_LOGI(TAG, "  [%zu]: float=%.6f, PCM=%d", j, sample_val, pcm_val);
                }
                
                signal_start_logged = true;
            }
            
            // Log amplitude statistics during chirp (first 2 seconds after detection)
            if (signal_start_logged && frames_processed < (size_t)fmt.sample_rate * 5) {
                static size_t last_amp_log = 0;
                if (frames_processed - last_amp_log >= (size_t)fmt.sample_rate / 2) {  // Every 0.5 seconds
                    float time_seconds = (float)frames_processed / (float)fmt.sample_rate;
                    ESP_LOGI(TAG, "Chirp amplitude at %.2fs: max=%.6f (PCM range: %d to %d)",
                             time_seconds, max_amp_this_chunk,
                             pcm_buffer[0], pcm_buffer[frames_this_chunk * fmt.num_channels - 1]);
                    last_amp_log = frames_processed;
                }
            }
            
            // Write PCM frames
            err = write_pcm_frames(pcm_buffer, frames_this_chunk, fmt.num_channels);
            
            frames_processed += frames_this_chunk;
            
            // Log when we reach 3 seconds (where the chirp should be)
            if (!three_second_logged && frames_processed >= three_second_frame) {
                float time_seconds = (float)frames_processed / (float)fmt.sample_rate;
                ESP_LOGI(TAG, "*** REACHED 3 SECOND MARK (chirp location) at frame %zu (%.2f seconds) ***", 
                         frames_processed, time_seconds);
                three_second_logged = true;
            }
            
            // Update progress callback if provided (after updating frames_processed)
            if (progress_cb && err == ESP_OK) {
                float progress = (float)frames_processed / (float)frame_count;
                if (progress > 1.0f) progress = 1.0f;
                progress_cb(progress, true);
            }
            
            // Log progress every 5 seconds
            static size_t last_log_frame = 0;
            if (frames_processed - last_log_frame >= fmt.sample_rate * 5) {
                float time_seconds = (float)frames_processed / (float)fmt.sample_rate;
                ESP_LOGI(TAG, "Playback progress: %.1f seconds (%.1f%%)", 
                         time_seconds, (float)frames_processed * 100.0f / (float)frame_count);
                last_log_frame = frames_processed;
            }
        }
        
        // Final progress update
        if (progress_cb) {
            progress_cb(1.0f, true);
            vTaskDelay(pdMS_TO_TICKS(50));
            progress_cb(0.0f, false);
        }
        
        free(float_buffer);
        free(pcm_buffer);
        return err;
    } else {
        // Direct PCM playback
        size_t sample_count = data_size / (fmt.bits_per_sample / 8);
        size_t frame_count = sample_count / fmt.num_channels;
        const int16_t *samples = (const int16_t *)data_ptr;
        
        // For PCM, we can update progress during playback
        if (progress_cb) {
            const size_t update_interval = 1024; // Update every 1024 frames
            size_t frames_written = 0;
            
            while (frames_written < frame_count) {
                size_t frames_this_batch = update_interval;
                if (frames_written + frames_this_batch > frame_count) {
                    frames_this_batch = frame_count - frames_written;
                }
                
                esp_err_t err = write_pcm_frames(samples + (frames_written * fmt.num_channels), 
                                                 frames_this_batch, fmt.num_channels);
                if (err != ESP_OK) {
                    return err;
                }
                
                frames_written += frames_this_batch;
                float progress = (float)frames_written / (float)frame_count;
                progress_cb(progress, true);
            }
            
            progress_cb(1.0f, true);
            vTaskDelay(pdMS_TO_TICKS(50));
            progress_cb(0.0f, false);
            return ESP_OK;
        } else {
            return write_pcm_frames(samples, frame_count, fmt.num_channels);
        }
    }
}

esp_err_t audio_player_submit_pcm(const int16_t *samples,
                                  size_t sample_count,
                                  int sample_rate_hz,
                                  int num_channels)
{
    ESP_RETURN_ON_FALSE(s_audio.initialized, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_ERROR(ensure_sample_rate(sample_rate_hz), TAG, "sr");
    return write_pcm_frames(samples, sample_count, num_channels);
}

void audio_player_shutdown(void)
{
    if (!s_audio.initialized) {
        return;
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // ESP-IDF 5.x: Disable and delete channel
    if (s_audio.tx_handle) {
        i2s_channel_disable(s_audio.tx_handle);
        i2s_del_channel(s_audio.tx_handle);
        s_audio.tx_handle = NULL;
    }
#else
    // ESP-IDF 4.x: Use legacy API
    i2s_driver_uninstall(s_audio.cfg.i2s_port);
#endif
    
    // Clean up I2C
    if (s_audio.i2c_bus != I2C_NUM_MAX) {
        i2c_driver_delete(s_audio.i2c_bus);
        s_audio.i2c_bus = I2C_NUM_MAX;
    }
    s_audio.i2c_dev = NULL;
    
    memset(&s_audio, 0, sizeof(s_audio));
}
