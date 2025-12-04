# Development Setup Guide

Complete instructions for setting up your development environment to build, flash, and monitor the Naphome-Korvo1 firmware.

## Quick Start (TL;DR)

```bash
# 1. Install ESP-IDF
git clone --branch v5.4 --depth 1 https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v5.4
cd ~/esp/esp-idf-v5.4
./install.sh esp32s3
. ./export.sh

# 2. Clone this repository
cd ~
git clone https://github.com/Naptick/Naphome-Korvo1.git
cd Naphome-Korvo1

# 3. Build, Flash, and Monitor
idf.py build
idf.py -p /dev/cu.usbserial-120 flash monitor
```

## Detailed Setup

### Prerequisites

- macOS, Linux, or Windows (WSL2)
- Python 3.8 or later
- Git
- USB cable to connect ESP32-S3 device

### 1. Install ESP-IDF

ESP-IDF is the official development framework for ESP32 microcontrollers.

#### Option A: Fresh Installation (Recommended)

```bash
# Create ESP directory
mkdir -p ~/esp
cd ~/esp

# Clone ESP-IDF v5.4
git clone --branch v5.4 --depth 1 https://github.com/espressif/esp-idf.git esp-idf-v5.4
cd esp-idf-v5.4

# Run the installer
./install.sh esp32s3

# This will:
# - Create a Python virtual environment
# - Install required tools (esptool, ninja, etc.)
# - Download ESP32-S3 toolchain
```

#### Option B: Using Existing Installation

If you already have ESP-IDF installed, verify it's v5.4:

```bash
cd $IDF_PATH
git describe --tags
# Should output: v5.4.0 (or similar)
```

### 2. Set Up Your Environment

Before each development session, source the ESP-IDF environment:

```bash
cd ~/esp/esp-idf-v5.4
. ./export.sh

# Verify setup
idf.py --version
# Should output: ESP-IDF v5.4.0
```

**Tip**: Add to your shell profile (`~/.zshrc`, `~/.bashrc`, etc.) to auto-load on terminal startup:

```bash
# ESP-IDF Setup
export IDF_PATH="$HOME/esp/esp-idf-v5.4"
alias idf-setup=". $IDF_PATH/export.sh"
```

Then just run `idf-setup` before development.

### 3. Clone Naphome-Korvo1

```bash
cd ~
git clone https://github.com/Naptick/Naphome-Korvo1.git
cd Naphome-Korvo1
```

### 4. Identify Your Serial Port

Connect the ESP32-S3 device via USB.

#### macOS/Linux
```bash
ls -la /dev/ttyUSB* /dev/ttyACM* /dev/cu.usbserial*
# Look for something like: /dev/cu.usbserial-120
```

#### Windows (PowerShell)
```powershell
[System.IO.Ports.SerialPort]::getportnames()
# Look for: COM3, COM4, etc.
```

### 5. Build the Project

```bash
cd ~/Naphome-Korvo1
idf.py build
```

Output should show:
```
...
[100%] Built target naphome-korvo1.elf
Project build complete. Your app information will be printed below.
```

### 6. Flash to Device

```bash
# Replace /dev/cu.usbserial-120 with your actual port
idf.py -p /dev/cu.usbserial-120 flash
```

Output should show:
```
...
Wrote 5765712 bytes (1976204 compressed) at 0x00010000 in 63.1 seconds...
Hash of data verified.
...
Hard resetting via RTS pin...
Done
```

### 7. Monitor Device Output

```bash
idf.py -p /dev/cu.usbserial-120 monitor
```

You should see boot logs and application output:
```
ESP-ROM:esp32s3-20210327
Build:Mar 27 2021
rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)
...
I (0) cpu_start: Starting scheduler on PRO CPU at offset...
...
```

Press `Ctrl+]` to exit monitor.

## Common Commands

### Build and Flash in One Step

```bash
idf.py -p /dev/cu.usbserial-120 flash monitor
```

### Build Only (No Flash)

```bash
idf.py build
```

### Clean Build (Rebuild Everything)

```bash
idf.py fullclean
idf.py build
```

### Erase Flash Memory

```bash
idf.py -p /dev/cu.usbserial-120 erase-flash
```

### Reconfigure Project

```bash
idf.py menuconfig
```

Then navigate to configure options, press `Esc` twice to save.

## Configuration

Key settings are in `sdkconfig.defaults`:

```
CONFIG_SPIRAM=y                      # Enable external PSRAM (8MB)
CONFIG_SPIRAM_MODE_QUAD=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_GEMINI_API_KEY="your-key"    # Google Gemini API key
```

## Troubleshooting

### Serial Port Not Found

**Problem**: `error: could not open port /dev/cu.usbserial-120`

**Solutions**:
- Verify USB cable is connected
- List available ports: `ls /dev/cu.*` or `ls /dev/ttyUSB*`
- Try different USB port on your computer
- Try different USB cable
- Check device is powered on (LED should indicate)

### Build Fails: "idf.py: not found"

**Problem**: Command not recognized

**Solutions**:
- Run `idf-setup` or `. ~/esp/esp-idf-v5.4/export.sh`
- Verify ESP-IDF path: `echo $IDF_PATH`

### Build Fails: "Python module not found"

**Problem**: `ModuleNotFoundError: No module named 'cryptography'`

**Solutions**:
```bash
# Reinstall ESP-IDF Python environment
cd ~/esp/esp-idf-v5.4
./install.sh esp32s3
. ./export.sh
```

### Device Boots Into Bootloader

**Problem**: Device stuck at "waiting for download" after flashing

**Solutions**:
```bash
# Erase flash and reflash
idf.py -p /dev/cu.usbserial-120 erase-flash
idf.py -p /dev/cu.usbserial-120 flash

# Or use esptool directly
esptool.py -p /dev/cu.usbserial-120 erase-flash
esptool.py -p /dev/cu.usbserial-120 write-flash @build/flash_args
```

### Monitor Shows No Output

**Problem**: Device appears to be running but no logs appear

**Solutions**:
- Verify correct serial port: `idf.py -p /dev/cu.usbserial-120 monitor`
- Check baud rate (default 115200)
- Device might be in a crash loop; check syslog:
  ```bash
  idf.py -p /dev/cu.usbserial-120 monitor --baud 115200
  ```

### "Task watchdog got triggered"

**Problem**: Device reboots with watchdog timeout message

**Solutions**:
- Increase FreeRTOS task watchdog timeout in `menuconfig` → `Component config` → `ESP System Settings` → `Task watchdog tick period`
- Or reduce logging/processing in interrupt handlers
- Check for blocking operations in critical tasks

## Development Workflow

### Typical Development Cycle

```bash
# 1. Start with ESP-IDF environment set up
idf-setup

# 2. Make code changes
vim main/app_main.c

# 3. Build and flash
idf.py -p /dev/cu.usbserial-120 flash monitor

# 4. Check output and debug
# Press Ctrl+] to exit monitor when done

# 5. Repeat from step 2
```

### Viewing Build Output

See detailed build information:

```bash
idf.py build -v
```

This shows all compiler commands executed.

### Analyzing Logs

Device logs contain useful information:

- `I (timestamp) TAG: message` = Info level
- `W (timestamp) TAG: message` = Warning level
- `E (timestamp) TAG: message` = Error level

Example:
```
I (0) boot: ESP-IDF v5.4.0 2nd stage bootloader
I (200) app_main: Starting application
W (1000) voice_assistant: WiFi not connected
E (2000) gemini_api: TTS request failed: 503
```

### USB Connection Issues

If the device frequently disconnects:

```bash
# Try different baud rate
idf.py -p /dev/cu.usbserial-120 monitor --baud 9600

# Or check device health
idf.py -p /dev/cu.usbserial-120 get_security_info
```

## Advanced: Connecting Directly to Serial Port

If `idf.py monitor` doesn't work:

```bash
# Using screen (macOS/Linux)
screen /dev/cu.usbserial-120 115200

# Exit: Ctrl+A, then :quit

# Using minicom (Linux)
minicom -D /dev/ttyUSB0 -b 115200

# Using picocom (cross-platform)
picocom -b 115200 /dev/cu.usbserial-120
```

## Next Steps

- See [GEMINI_INTEGRATION.md](GEMINI_INTEGRATION.md) for API setup
- See [API_KEY_SETUP.md](API_KEY_SETUP.md) for credentials
- Check device logs in [logs/](logs/) directory

## Getting Help

If you encounter issues:

1. Check the [Troubleshooting](#troubleshooting) section above
2. Review ESP-IDF documentation: https://docs.espressif.com/projects/esp-idf/
3. Check device logs for error messages
4. Try a clean build: `idf.py fullclean && idf.py build`
