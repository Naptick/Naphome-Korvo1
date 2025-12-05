# Scripts

Utility scripts for the Korvo1 project.

## SD Card Setup

### `copy_sounds_to_sdcard.sh`
Copies all MP3 files to an SD card for use with the Korvo1 device.

**Usage:**
```bash
./copy_sounds_to_sdcard.sh /path/to/mounted/sdcard
```

See [SD_CARD_SETUP.md](./SD_CARD_SETUP.md) for detailed instructions.

---

# Audio Measurement Scripts

Scripts to automate audio measurements with REW or command-line tools.

## Quick Start

### Option 1: Interactive Helper Script (Recommended)

```bash
./scripts/rew_measure.sh
```

This script will guide you through:
- Recording audio measurements
- Opening REW for analysis
- Using SoX or Python for automated recording

### Option 2: Python Recording Script

```bash
# Install dependencies first
pip install sounddevice soundfile numpy

# Record a measurement
python3 scripts/record_measurement.py --duration 5 --output measurement.wav

# List available audio devices
python3 scripts/record_measurement.py --list-devices
```

### Option 3: SoX Command-Line Recording

```bash
# Install SoX (macOS)
brew install sox

# Record 5 seconds at 48kHz stereo
sox -d -r 48000 -c 2 -b 16 measurement.wav trim 0 5
```

## Workflow

1. **Flash your ESP32 device** with the log sweep firmware
2. **Connect audio interface** (microphone or line input) to your computer
3. **Run recording script** - it will wait for you to press Enter, then you trigger playback on the ESP32
4. **Import to REW** - The script can automatically open REW with your recording, or you can manually import

## REW Import

After recording, import the WAV file into REW:
1. Open REW
2. File → Import → Import measurement
3. Select your recorded WAV file
4. REW will analyze the frequency response

## Notes

- **REW doesn't have a native CLI**, but you can automate recording and then import to REW
- The Python script uses `sounddevice` which provides better control than SoX
- Make sure your audio interface sample rate matches (48kHz for the current firmware)
- Recordings are saved to `measurements/` directory (gitignored)
