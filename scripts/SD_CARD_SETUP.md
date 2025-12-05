# SD Card Setup for MP3 Files

This guide explains how to load MP3 files onto an SD card for use with the Korvo1 device.

## Overview

The Korvo1 firmware supports loading MP3 files from an SD card. The device will automatically:
1. Try to mount the SD card at `/sdcard`
2. Look for MP3 files in `/sdcard/sounds/` directory
3. Fall back to SPIFFS if SD card is not available

## SD Card Requirements

- **Format**: FAT32 (recommended) or FAT16
- **Capacity**: Any size (tested with 2GB - 32GB cards)
- **File System**: Must be formatted as FAT32/FAT16 (not exFAT or NTFS)

**📋 Need to format your SD card?** See [FORMAT_SD_CARD.md](./FORMAT_SD_CARD.md) for detailed formatting instructions.

## Hardware Connection

The Korvo1 supports SD cards via:
- **SDMMC mode** (1-bit mode, preferred): Uses dedicated SDMMC pins
- **SPI mode** (fallback): Uses SPI pins if SDMMC is not available

### SDMMC Pin Configuration (Default)
- CLK: GPIO14
- CMD: GPIO15  
- D0: GPIO2
- D1: GPIO4 (optional, for 4-bit mode)
- D2: GPIO12 (optional, for 4-bit mode)
- D3: GPIO13 (optional, for 4-bit mode)

### SPI Mode Pin Configuration (Fallback)
- MOSI: GPIO11
- MISO: GPIO13
- SCLK: GPIO12
- CS: GPIO10

**Note**: These GPIO pins may need adjustment based on your specific hardware configuration.

## Copying Files to SD Card

### Method 1: Using the Provided Script (Recommended)

1. Insert the SD card into your computer's SD card reader
2. Mount the SD card (it should appear as a drive)
3. Run the copy script:

```bash
cd scripts
./copy_sounds_to_sdcard.sh /path/to/mounted/sdcard
```

**Examples:**
- macOS: `./copy_sounds_to_sdcard.sh /Volumes/SDCARD`
- Linux: `./copy_sounds_to_sdcard.sh /media/user/SDCARD`
- Windows (WSL): `./copy_sounds_to_sdcard.sh /mnt/d`

The script will:
- Create a `sounds` directory on the SD card
- Copy all 163 MP3 files from the `sounds/` directory
- Show progress as files are copied

### Method 2: Manual Copy

1. Insert the SD card into your computer
2. Create a `sounds` directory on the SD card
3. Copy all MP3 files from the project's `sounds/` directory to the SD card's `sounds/` directory:

```bash
# Example on macOS/Linux
mkdir -p /Volumes/SDCARD/sounds
cp sounds/*.mp3 /Volumes/SDCARD/sounds/
```

## Verifying Files on SD Card

After copying, verify the files are present:

```bash
# Check file count
ls /path/to/sdcard/sounds/*.mp3 | wc -l
# Should show 163 files

# Check a few files
ls /path/to/sdcard/sounds/ | head -10
```

## Using the SD Card with Korvo1

1. **Power off** the Korvo1 device
2. **Insert the SD card** into the SD card slot
3. **Power on** the device
4. The device will automatically:
   - Detect and mount the SD card
   - Scan for MP3 files in `/sdcard/sounds/`
   - Make them available in the web interface

## Checking SD Card Status

Once the device is running, you can check if the SD card was detected:

1. **Via Serial Logs**: Look for messages like:
   ```
   ✅ SD card mounted at /sdcard
   ✅ Audio file manager initialized with 163 tracks
   ```

2. **Via Web Interface**: 
   - Navigate to `http://nap.local/`
   - Check the MP3 Player section
   - The dropdown should show all available tracks

## Troubleshooting

### SD Card Not Detected

1. **Check card format**: Ensure it's FAT32 or FAT16
2. **Check card size**: Very large cards (>32GB) may need special formatting
3. **Check GPIO pins**: Verify the SD card is connected to the correct pins
4. **Check serial logs**: Look for error messages about SD card mounting

### Files Not Found

1. **Check directory structure**: Files must be in `/sdcard/sounds/` directory
2. **Check file names**: Ensure files have `.mp3` extension
3. **Check file count**: Verify all 163 files were copied
4. **Check logs**: Look for messages about file discovery

### SD Card Mount Fails

If SDMMC mode fails, the device will automatically try SPI mode. If both fail:
- Check hardware connections
- Verify GPIO pin configuration matches your hardware
- Check if SD card is properly inserted
- Try a different SD card

## File Organization

The SD card structure should be:
```
/sdcard/
  └── sounds/
      ├── 10min_meditation_anxiety.mp3
      ├── 10min_meditation_mindfulness.mp3
      ├── ...
      └── woodland_calm_wakeup.mp3
```

## Performance Notes

- **SD card speed**: Class 10 or higher recommended for smooth playback
- **File size**: Individual MP3 files can be any size
- **Total size**: All 163 files together are approximately several hundred MB
- **Playback**: Files are streamed from SD card, so large files are supported

## Fallback Behavior

If the SD card is not available, the device will:
1. Try to load files from SPIFFS (`/spiffs/sounds/`)
2. Use the static file list (files won't be playable until uploaded)

This ensures the device continues to function even without an SD card.
