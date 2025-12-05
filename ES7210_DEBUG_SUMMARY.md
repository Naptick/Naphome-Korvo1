# ES7210 Debugging Summary

## Current Status

- ✅ **I2C Communication**: Working - ES7210 detected at address 0x40
- ✅ **Register Writes**: All register writes succeed and read back correctly
- ❌ **Audio Output**: All samples are zeros (0x00000000)

## Register Values After Initialization

From the logs:
- Reg 0x00: 0x00 (Reset - cleared)
- Reg 0x08: 0xFF (Clock ON - enabled)
- Reg 0x09: 0x00 (Master clock - use MCLK pin)
- Reg 0x0A: 0x08 (ADC MCLK divider)
- Reg 0x0B: 0x00 (Frame sync clock)
- Reg 0x0C: 0x00 (Sample rate)
- Reg 0x0D: 0x0F (Analog path - all mics enabled)
- Reg 0x0E: 0xFF (Digital path - read-only)
- Reg 0x0F: 0xFF (Digital pre-filter - read-only)
- Reg 0x10: 0x01 (ADC control - enabled)

## ESP-SKAINET Reference

From `esp-skainet/components/hardware_driver/boards/esp32s3-korvo-1/bsp_board.c`:

```c
es7210_codec_cfg_t es7210_cfg = {
    .ctrl_if = record_ctrl_if,
    .mic_selected = ES7120_SEL_MIC1 | ES7120_SEL_MIC2 | ES7120_SEL_MIC3 | ES7120_SEL_MIC4,
};
record_codec_if = es7210_codec_new(&es7210_cfg);

esp_codec_dev_sample_info_t fs = {
    .sample_rate = 16000,
    .channel = 2,
    .bits_per_sample = 32,
};
esp_codec_dev_open(record_dev, &fs);
```

**Key Points:**
- Uses high-level `es7210_codec_new()` API from ESP codec component
- All low-level register writes are abstracted
- Sample rate: 16000 Hz
- Channels: 2 (stereo)
- Bits per sample: 32 bits
- All 4 microphones enabled

## Possible Issues

1. **MCLK Divider Value**: Current value 0x08 might be incorrect
   - I2S provides MCLK = 20.48 MHz
   - ES7210 needs MCLK = 4.096 MHz (256 * 16kHz)
   - Divider should be 20.48 / 4.096 = 5
   - But ES7210 divider encoding might be different

2. **Sample Rate Register**: Register 0x0C = 0x00 might not be correct for 16kHz
   - May need specific value like 0x10, 0x20, or other

3. **Frame Sync Clock**: Register 0x0B = 0x00 might need adjustment

4. **Missing Register Configuration**: There might be additional registers that need to be set

5. **I2S Clock Mismatch**: The I2S might not be providing the correct clock frequency

## Recommended Solutions

### Option 1: Use ESP Codec Component (Recommended)

Add the ESP codec component as a submodule and use the high-level API:

```c
#include "es7210_codec.h"

es7210_codec_cfg_t es7210_cfg = {
    .ctrl_if = i2c_ctrl_if,
    .mic_selected = ES7120_SEL_MIC1 | ES7120_SEL_MIC2 | ES7120_SEL_MIC3 | ES7120_SEL_MIC4,
};
codec_handle = es7210_codec_new(&es7210_cfg);
```

### Option 2: Find ES7210 Datasheet

Get the official ES7210 datasheet to find:
- Correct register addresses
- Correct register values for 16kHz
- MCLK divider encoding
- Complete initialization sequence

### Option 3: Try Different Register Values

Based on common codec patterns, try:
- Register 0x0A (MCLK divider): 0x05, 0x04, 0x10, 0x20
- Register 0x0B (Frame sync): 0x01, 0x02, 0x10
- Register 0x0C (Sample rate): 0x10, 0x20, 0x30

### Option 4: Verify I2S Configuration

Check that I2S is actually providing:
- MCLK = 20.48 MHz on GPIO20
- BCLK = correct frequency
- LRCK = 16kHz

## Next Steps

1. Try adding ESP codec component as submodule
2. Search for ES7210 datasheet online
3. Test different register values systematically
4. Verify I2S clock output with oscilloscope/logic analyzer
5. Check if ES7210 requires power-on sequence or additional GPIO control
