# ES7210 ADC Initialization Analysis

## ESP-SKAINET Usage Pattern

From `esp-skainet/components/hardware_driver/boards/esp32s3-korvo-1/bsp_board.c`:

### Key Findings:

1. **I2S Configuration:**
   - Uses `I2S_NUM_1` for microphone (ES7210)
   - Sample rate: 16000 Hz
   - Channels: 2 (stereo)
   - Bits per sample: 32 bits
   - GPIO pins: `GPIO_I2S_SCLK`, `GPIO_I2S_LRCK`, `GPIO_I2S_SDIN`, `GPIO_I2S_MCLK`

2. **ESP Codec Dev Library:**
   - Uses high-level `es7210_codec_new()` API
   - Mic selection: `ES7120_SEL_MIC1 | ES7120_SEL_MIC2 | ES7120_SEL_MIC3 | ES7120_SEL_MIC4`
   - I2C address: `ES7210_CODEC_DEFAULT_ADDR` (likely 0x40)
   - All low-level register writes are abstracted by the library

3. **Initialization Flow:**
   ```
   bsp_i2c_init() → bsp_i2s_init(I2S_NUM_1) → bsp_codec_adc_init()
   ```

## Current Implementation Issues

1. **Chip ID reads 0xFF** - This suggests:
   - Wrong I2C address (should verify 0x40)
   - I2C communication issue
   - Chip not powered/reset properly
   - Wrong register address for chip ID

2. **Microphone reading all zeros** - ES7210 not outputting audio:
   - May need proper clock configuration
   - May need microphone input selection
   - May need gain/analog path configuration

## Recommended ES7210 Initialization Sequence

Based on typical ADC codec initialization patterns:

1. **Reset** (0x00 = 0xFF then 0x00)
2. **Clock Configuration:**
   - Enable clocks (0x08 = 0xFF)
   - Configure MCLK divider for 16kHz
   - Configure sample rate
3. **Analog Path:**
   - Select microphone inputs
   - Configure gain
4. **Digital Path:**
   - Enable ADC digital processing
   - Configure data format
5. **Enable ADC:**
   - Start ADC conversion

## Next Steps

1. Verify I2C address (try scanning bus)
2. Check if ES7210 needs different register addresses
3. Look for ES7210 datasheet for correct register values
4. Consider using ESP codec component as submodule if available
5. Test with different initialization sequences
