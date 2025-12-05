# How to Format SD Card for Korvo1

The Korvo1 device requires SD cards to be formatted as **FAT32** (or FAT16). This guide provides step-by-step instructions for macOS.

## Requirements

- **Format**: FAT32 (recommended) or FAT16
- **Capacity**: Any size (tested with 2GB - 32GB cards)
- **File System**: Must be FAT32/FAT16 (NOT exFAT or NTFS)

## Method 1: Using Disk Utility (GUI - Recommended)

### Step 1: Open Disk Utility
1. Open **Finder**
2. Go to **Applications** → **Utilities** → **Disk Utility**
   - Or press `Cmd + Space` and type "Disk Utility"

### Step 2: Select Your SD Card
1. In the left sidebar, find your SD card
2. **Important**: Select the **disk** (not the volume/partition)
   - Look for something like "SD Card" or "NO NAME" at the top level
   - It should show the physical device, not a mounted volume

### Step 3: Erase the SD Card
1. Click the **Erase** button (top toolbar)
2. In the erase dialog:
   - **Name**: Enter a name (e.g., "KORVO1" or "SDCARD")
   - **Format**: Select **MS-DOS (FAT32)**
   - **Scheme**: Select **Master Boot Record (MBR)**
3. Click **Erase**
4. Wait for the format to complete (may take a few minutes)

### Step 4: Verify Format
1. The SD card should now appear in Finder
2. You can verify the format by:
   - Right-clicking the SD card in Finder → **Get Info**
   - It should show "Format: MS-DOS (FAT32)"

## Method 2: Using Command Line (Terminal)

### Step 1: Find Your SD Card Device
```bash
# List all disks
diskutil list

# Look for your SD card (usually shows as /dev/disk2 or similar)
# Example output:
# /dev/disk2 (external, physical):
#    #:                       TYPE NAME                    SIZE       IDENTIFIER
#    0:     FDisk_partition_scheme                        *15.9 GB    disk2
#    1:                 DOS_FAT_32 SDCARD                   15.9 GB    disk2s1
```

### Step 2: Unmount the SD Card
```bash
# Replace disk2 with your actual disk identifier
diskutil unmountDisk /dev/disk2
```

### Step 3: Format as FAT32
```bash
# Format with MBR scheme and FAT32
# Replace disk2 with your actual disk identifier
# Replace "KORVO1" with your desired volume name
sudo diskutil eraseDisk FAT32 KORVO1 MBRFormat /dev/disk2
```

**Important Notes:**
- You'll need to enter your password (sudo)
- Replace `disk2` with your actual disk identifier from step 1
- Replace `KORVO1` with your desired volume name
- This will **erase all data** on the SD card

### Step 4: Verify Format
```bash
# Check the format
diskutil info /dev/disk2s1

# Should show:
# File System Personality: MS-DOS FAT32
```

## Troubleshooting

### "MS-DOS (FAT32)" Option Not Available

If Disk Utility doesn't show "MS-DOS (FAT32)" as an option:
- The card might be too large (>32GB)
- **Solution**: Use command line method with `diskutil` (see Method 2)
- Or use a smaller SD card (≤32GB)

### SD Card Larger Than 32GB

FAT32 has a 32GB limit in some tools, but you can still format larger cards:
```bash
# For cards >32GB, use command line with explicit FAT32
sudo diskutil eraseDisk FAT32 KORVO1 MBRFormat /dev/disk2
```

### "Resource Busy" Error

If you get a "resource busy" error:
```bash
# First unmount the disk
diskutil unmountDisk /dev/disk2

# Then try formatting again
sudo diskutil eraseDisk FAT32 KORVO1 MBRFormat /dev/disk2
```

### Wrong Disk Selected

**⚠️ WARNING**: Formatting the wrong disk will erase all data!

To be safe:
1. **Unplug all external drives** except the SD card
2. **Double-check** the disk identifier before formatting
3. Use `diskutil list` to verify which disk is your SD card

## After Formatting

Once formatted, you can:

1. **Copy MP3 files** using the provided script:
   ```bash
   ./scripts/copy_sounds_to_sdcard.sh /Volumes/KORVO1
   ```

2. **Or manually copy** files:
   ```bash
   mkdir -p /Volumes/KORVO1/sounds
   cp sounds/*.mp3 /Volumes/KORVO1/sounds/
   ```

3. **Insert into Korvo1** device and power on

## Quick Reference

```bash
# 1. List disks
diskutil list

# 2. Unmount SD card (replace disk2 with your disk)
diskutil unmountDisk /dev/disk2

# 3. Format as FAT32 (replace disk2 and KORVO1)
sudo diskutil eraseDisk FAT32 KORVO1 MBRFormat /dev/disk2

# 4. Verify
diskutil info /dev/disk2s1
```

## Format Specifications

- **File System**: FAT32
- **Partition Scheme**: MBR (Master Boot Record)
- **Volume Name**: Any name (e.g., "KORVO1", "SDCARD")
- **Cluster Size**: Default (automatically set)

The ESP32 SDMMC driver will automatically detect and mount FAT32 formatted cards.
