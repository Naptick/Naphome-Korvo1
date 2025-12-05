# SD Card Troubleshooting Guide

Even if your SD card is already formatted as FAT32, you may encounter errors. This guide covers common issues and solutions.

## Common Error Messages

### 1. "SDMMC mount failed: ESP_ERR_TIMEOUT"
**Cause**: Card not detected or communication timeout
**Solutions**:
- Check physical connection (card fully inserted)
- Verify GPIO pin connections match your hardware
- Try a different SD card
- Check if card is locked (write-protect switch)

### 2. "SDMMC mount failed: ESP_ERR_NOT_FOUND"
**Cause**: Card not present or not responding
**Solutions**:
- Ensure card is properly inserted
- Try re-inserting the card
- Check if card works in a computer reader

### 3. "SDMMC mount failed: ESP_FAIL"
**Cause**: Filesystem corruption or incompatible format
**Solutions**:
- Repair filesystem on computer (see below)
- Reformat the card (even if already FAT32)
- Check partition scheme (must be MBR, not GPT)

### 4. Card Detected But Files Not Found
**Cause**: Directory structure or file system issues
**Solutions**:
- Verify `/sdcard/sounds/` directory exists
- Check file permissions
- Ensure files have `.mp3` extension

## Fixing FAT32 Cards That Still Error

### Step 1: Verify Current Format

On macOS:
```bash
# Check format
diskutil info /dev/disk2s1 | grep "File System Personality"

# Should show: "File System Personality: MS-DOS FAT32"
```

### Step 2: Check Partition Scheme

The ESP32 requires **MBR** (Master Boot Record) partition scheme, not GPT:

```bash
# Check partition scheme
diskutil list /dev/disk2

# Look for "FDisk_partition_scheme" (MBR) or "GUID_partition_scheme" (GPT)
# If it shows GPT, you need to reformat with MBR
```

### Step 3: Repair Filesystem (If Corrupted)

Even FAT32 cards can have filesystem errors:

**On macOS:**
```bash
# Unmount first
diskutil unmountDisk /dev/disk2

# Run filesystem check (read-only)
sudo fsck_msdos -n /dev/disk2s1

# If errors found, repair:
sudo fsck_msdos -y /dev/disk2s1
```

**On Windows:**
```cmd
# Open Command Prompt as Administrator
chkdsk E: /f
# (Replace E: with your SD card drive letter)
```

**On Linux:**
```bash
# Unmount first
sudo umount /dev/sdb1

# Check filesystem
sudo fsck.vfat -n /dev/sdb1

# Repair if needed
sudo fsck.vfat -a /dev/sdb1
```

### Step 4: Reformat (Even If Already FAT32)

Sometimes a fresh format fixes issues:

**macOS (Command Line):**
```bash
# 1. Unmount
diskutil unmountDisk /dev/disk2

# 2. Reformat with MBR scheme (important!)
sudo diskutil eraseDisk FAT32 KORVO1 MBRFormat /dev/disk2

# 3. Verify
diskutil info /dev/disk2s1
```

**macOS (Disk Utility):**
1. Open Disk Utility
2. Select SD card (disk, not volume)
3. Click **Erase**
4. **Format**: MS-DOS (FAT32)
5. **Scheme**: **Master Boot Record (MBR)** ← Important!
6. Click **Erase**

### Step 5: Verify Allocation Unit Size

Large allocation units can cause issues. The ESP32 code uses 16KB allocation units, which should work with most cards. If you have issues, try:

```bash
# On macOS, you can't directly set cluster size, but you can:
# - Use a smaller card (<32GB) which typically uses smaller clusters
# - Or reformat with specific tools (see below)
```

## Advanced Formatting Options

### Using Third-Party Tools

If standard formatting doesn't work, try:

**macOS:**
- **SD Card Formatter** (official SD Association tool)
  - Download from: https://www.sdcard.org/downloads/formatter/
  - Ensures proper SD card formatting

**Windows:**
- **SD Card Formatter** (official tool)
- **Rufus** (can format with specific options)

**Linux:**
```bash
# Use mkfs.vfat with specific options
sudo mkfs.vfat -F 32 -n KORVO1 /dev/sdb1
```

### Formatting Large Cards (>32GB)

Cards larger than 32GB may need special handling:

```bash
# On macOS, use command line to force FAT32 on large cards
sudo diskutil eraseDisk FAT32 KORVO1 MBRFormat /dev/disk2

# If that doesn't work, use SD Card Formatter tool
```

## Hardware Troubleshooting

### Check GPIO Pin Connections

Verify your SD card is connected to the correct pins:

**SDMMC Mode (1-bit, default):**
- CLK: GPIO14
- CMD: GPIO15
- D0: GPIO2
- VDD: 3.3V
- GND: Ground

**SPI Mode (fallback):**
- MOSI: GPIO11
- MISO: GPIO13
- SCLK: GPIO12
- CS: GPIO10

### Test Card in Computer

1. Insert card in computer
2. Verify it mounts and is readable
3. Try copying a test file
4. If it fails on computer, the card may be damaged

### Try Different Card

- Some cards are incompatible with ESP32
- Try a different brand or size
- Class 10 cards generally work better
- Older/slower cards sometimes work more reliably

## Software Configuration

### Check ESP32 Logs

Look for specific error codes in serial output:

```bash
# Monitor serial output
idf.py monitor

# Look for:
# - "SDMMC mount failed: [ERROR_CODE]"
# - Card detection messages
# - Filesystem mount status
```

### Enable More Verbose Logging

You can temporarily increase SD card logging by modifying `app_main.c`:

```c
// Change log level for SD card operations
ESP_LOGI(TAG, "Initializing SD card (SDMMC mode)...");
// Add more detailed error logging
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SDMMC mount failed: %s (0x%x)", 
             esp_err_to_name(ret), ret);
    ESP_LOGE(TAG, "Card present: %d", /* check card detect pin */);
}
```

## Common Solutions Summary

| Error | Most Likely Cause | Quick Fix |
|-------|------------------|-----------|
| ESP_ERR_TIMEOUT | Card not connected | Check physical connection |
| ESP_ERR_NOT_FOUND | Card not present | Re-insert card |
| ESP_FAIL | Filesystem corruption | Repair or reformat |
| Files not found | Wrong directory | Create `/sdcard/sounds/` |
| GPT partition | Wrong partition scheme | Reformat with MBR |

## Verification Checklist

After formatting, verify:

- [ ] Format is FAT32 (MS-DOS)
- [ ] Partition scheme is MBR (not GPT)
- [ ] Card mounts on computer
- [ ] Can create/delete files on computer
- [ ] Card inserted properly in ESP32
- [ ] GPIO pins connected correctly
- [ ] Serial logs show card detection
- [ ] `/sdcard/sounds/` directory exists (or will be created)

## Still Having Issues?

1. **Check serial logs** for specific error codes
2. **Try a different SD card** (different brand/size)
3. **Verify hardware connections** (GPIO pins, power, ground)
4. **Test card in computer** first to rule out card issues
5. **Try SPI mode** if SDMMC fails (may need code changes)

## Quick Test Script

After formatting, test the card:

```bash
# 1. Format card
sudo diskutil eraseDisk FAT32 KORVO1 MBRFormat /dev/disk2

# 2. Create test structure
mkdir -p /Volumes/KORVO1/sounds
echo "test" > /Volumes/KORVO1/sounds/test.txt

# 3. Verify
ls -la /Volumes/KORVO1/sounds/

# 4. Eject properly
diskutil eject /dev/disk2

# 5. Insert in ESP32 and check logs
```
