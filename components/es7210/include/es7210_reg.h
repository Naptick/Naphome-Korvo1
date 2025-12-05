/**
 * @file es7210_reg.h
 * @brief ES7210 ADC codec register definitions
 * 
 * Based on register dump analysis and ESP-SKAINET usage patterns.
 * Note: Some registers may need verification with official ES7210 datasheet.
 */

#ifndef ES7210_REG_H
#define ES7210_REG_H

#ifdef __cplusplus
extern "C" {
#endif

// ES7210 I2C address (7-bit)
#define ES7210_I2C_ADDR_7BIT      0x40

// ES7210 register addresses
#define ES7210_REG_00_RESET       0x00  // Reset register
#define ES7210_REG_01             0x01  // Unknown (reads 0x20)
#define ES7210_REG_02             0x02  // Unknown (reads 0x02)
#define ES7210_REG_03             0x03  // Unknown (reads 0x04)
#define ES7210_REG_04             0x04  // Unknown (reads 0x01)
#define ES7210_REG_05             0x05  // Unknown (reads 0x00)
#define ES7210_REG_06             0x06  // Unknown (reads 0x00)
#define ES7210_REG_07             0x07  // Unknown (reads 0x20)
#define ES7210_REG_08_CLOCK_ON    0x08  // Clock enable (writable, reads 0xFF when enabled)
#define ES7210_REG_09_MASTER_CLK  0x09  // Master clock configuration
#define ES7210_REG_0A_ADC_MCLK    0x0A  // ADC MCLK divider (reads 0x08)
#define ES7210_REG_0B_ADC_FSCLK   0x0B  // ADC frame sync clock
#define ES7210_REG_0C_ADC_SAMPLE  0x0C  // ADC sample rate configuration
#define ES7210_REG_0D_ADC_ANALOG  0x0D  // Analog path / microphone selection (writable)
#define ES7210_REG_0E_ADC_DIGITAL 0x0E  // Digital path (read-only, always 0xFF)
#define ES7210_REG_0F_ADC_DPF     0x0F  // Digital pre-filter (read-only, always 0xFF)
#define ES7210_REG_10_ADC_CTRL    0x10  // ADC control (writable, reads 0x01 when enabled)
#define ES7210_REG_FD_CHIP_ID     0xFD  // Chip ID register (should read 0x21, but reads 0xFF)

// Register 0x00: Reset
#define ES7210_RESET_VALUE        0xFF  // Write to reset
#define ES7210_RESET_CLEAR        0x00  // Write to clear reset

// Register 0x08: Clock enable
#define ES7210_CLOCK_ON_ALL       0xFF  // Enable all clocks

// Register 0x0D: Analog path / Microphone selection
// Bit flags for microphone selection (based on ESP-SKAINET: ES7120_SEL_MIC1 | ES7120_SEL_MIC2 | ES7120_SEL_MIC3 | ES7120_SEL_MIC4)
#define ES7210_MIC_SEL_MIC1       0x01
#define ES7210_MIC_SEL_MIC2       0x02
#define ES7210_MIC_SEL_MIC3       0x04
#define ES7210_MIC_SEL_MIC4       0x08
#define ES7210_MIC_SEL_ALL        0x0F  // All 4 microphones

// Register 0x10: ADC control
#define ES7210_ADC_CTRL_ENABLE    0x01  // Enable ADC

#ifdef __cplusplus
}
#endif

#endif // ES7210_REG_H
