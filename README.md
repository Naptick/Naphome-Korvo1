# Naphome-Korvo1

ESP32-S3 firmware for Korvo1 development board featuring LED control and audio playback with log sweep test tones.

🌐 **[View Full Features List →](https://naptick.github.io/Naphome-Korvo1/features.html)**

## Features

- **LED Control**: WS2812 RGB LED ring control with configurable brightness and animations
- **Audio Playback**: ES8311 codec support for high-quality audio output
- **Log Sweep Generator**: Generates logarithmic frequency sweeps (chirp signals) for audio testing
- **MP3 Support**: Optional MP3 file playback using Helix MP3 decoder

## Hardware

- **Board**: ESP32-S3 based Korvo1 development board
- **LEDs**: WS2812 RGB LED ring (12 LEDs, GPIO 19)
- **Audio Codec**: ES8311 (I2S + I2C)
  - I2S: BCLK=GPIO4, LRCLK=GPIO5, DATA=GPIO18, MCLK=GPIO0
  - I2C: SCL=GPIO1, SDA=GPIO2

## Project Structure

```
Naphome-Korvo1/
├── main/                    # Main application code
│   ├── app_main.c          # Main entry point
│   ├── audio_player.c      # ES8311 audio codec driver
│   ├── audio_player.h
│   └── CMakeLists.txt
├── components/             # ESP-IDF components
│   ├── led_strip/         # WS2812 LED strip driver
│   └── helix_mp3/         # MP3 decoder
├── drivers/               # Hardware drivers
│   └── audio/
│       └── korvo1/        # Korvo1 microphone driver
├── CMakeLists.txt         # Root CMake configuration
├── sdkconfig.defaults     # Default ESP-IDF configuration
└── README.md
```

## Building

### Prerequisites

- ESP-IDF v4.4 or later
- Python 3.8+
- CMake 3.16+

### Build Steps

1. Clone the repository:
```bash
git clone https://github.com/Naptick/Naphome-Korvo1.git
cd Naphome-Korvo1
```

2. Set up ESP-IDF (if not already done):
```bash
. $IDF_PATH/export.sh
```

3. Configure the project:
```bash
idf.py menuconfig
```

Key configuration options:
- **Korvo1 LED Audio Test → WS2812 data GPIO**: GPIO pin for LED data (default: 19)
- **Korvo1 LED Audio Test → Number of pixels**: LED count (default: 12)
- **Korvo1 LED Audio Test → Brightness**: 0-255 (default: 64)
- **Korvo1 LED Audio Test → Audio sample rate**: Sample rate in Hz (default: 44100)
- **Korvo1 LED Audio Test → Log sweep duration**: Duration in seconds (default: 5)
- **Korvo1 LED Audio Test → Log sweep start frequency**: Start frequency in Hz (default: 20)
- **Korvo1 LED Audio Test → Log sweep end frequency**: End frequency in Hz (default: 20000)

4. Build the project:
```bash
idf.py build
```

5. Flash to device:
```bash
idf.py flash monitor
```

## Usage

After flashing, the device will:
1. Initialize the LED ring with a startup animation
2. Continuously play logarithmic frequency sweeps (20 Hz to 20 kHz over 5 seconds)
3. Animate LEDs in sync with audio playback (rainbow sweep effect)

The log sweep is useful for:
- Audio system testing
- Frequency response analysis
- Speaker/headphone testing
- Audio calibration

## Configuration

All configuration is done through `menuconfig` or by editing `sdkconfig.defaults`. The default configuration is optimized for the Korvo1 development board.

### LED Configuration

- **GPIO**: GPIO 19 (configurable)
- **Count**: 12 LEDs (configurable)
- **Brightness**: 64/255 (configurable)

### Audio Configuration

- **Sample Rate**: 44100 Hz (configurable)
- **Codec**: ES8311
- **Format**: 16-bit PCM, stereo

### Log Sweep Parameters

- **Duration**: 5 seconds (configurable)
- **Start Frequency**: 20 Hz (configurable)
- **End Frequency**: 20000 Hz (configurable)
- **Type**: Logarithmic (chirp)

## Development

### Adding MP3 Playback

The code includes MP3 decoder support. To play an MP3 file:

1. Embed the MP3 file as a byte array in your code
2. Call `play_mp3_file()` with the MP3 data and length
3. LEDs will animate during playback

### Extending LED Animations

LED animations are controlled in `update_leds_for_audio()`. You can modify this function to create custom animations synchronized with audio playback.

## Troubleshooting

### Audio Not Playing

- Check I2S and I2C pin connections
- Verify ES8311 codec is properly initialized
- Check serial monitor for error messages

### LEDs Not Working

- Verify GPIO 19 is connected to LED data line
- Check LED count configuration matches hardware
- Ensure power supply can handle LED current draw

### Build Errors

- Ensure ESP-IDF is properly installed and sourced
- Check that all components are present in `components/` directory
- Verify CMake version is 3.16 or later

## License

[Add your license here]

## Contributing

[Add contribution guidelines here]

## Acknowledgments

- ESP-IDF team for the excellent framework
- Espressif for the Korvo1 development board
- Helix MP3 decoder project
