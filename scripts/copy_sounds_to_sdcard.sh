#!/bin/bash
# Script to copy MP3 files to SD card
# Usage: ./copy_sounds_to_sdcard.sh /path/to/mounted/sdcard

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SOUNDS_DIR="$PROJECT_ROOT/sounds"

if [ $# -lt 1 ]; then
    echo "Usage: $0 /path/to/mounted/sdcard"
    echo ""
    echo "Example:"
    echo "  $0 /Volumes/SDCARD"
    echo "  $0 /media/user/SDCARD"
    echo ""
    echo "This script will:"
    echo "  1. Create a 'sounds' directory on the SD card"
    echo "  2. Copy all MP3 files from $SOUNDS_DIR to the SD card"
    exit 1
fi

SDCARD_PATH="$1"
SOUNDS_DEST="$SDCARD_PATH/sounds"

if [ ! -d "$SDCARD_PATH" ]; then
    echo "Error: SD card path does not exist: $SDCARD_PATH"
    exit 1
fi

if [ ! -d "$SOUNDS_DIR" ]; then
    echo "Error: Sounds directory not found: $SOUNDS_DIR"
    exit 1
fi

echo "Copying MP3 files to SD card..."
echo "  Source: $SOUNDS_DIR"
echo "  Destination: $SOUNDS_DEST"
echo ""

# Create sounds directory on SD card
mkdir -p "$SOUNDS_DEST"

# Count files
FILE_COUNT=$(find "$SOUNDS_DIR" -name "*.mp3" | wc -l | tr -d ' ')
echo "Found $FILE_COUNT MP3 files to copy"
echo ""

# Copy files with progress
COPIED=0
for file in "$SOUNDS_DIR"/*.mp3; do
    if [ -f "$file" ]; then
        filename=$(basename "$file")
        cp "$file" "$SOUNDS_DEST/"
        COPIED=$((COPIED + 1))
        if [ $((COPIED % 10)) -eq 0 ]; then
            echo "  Copied $COPIED/$FILE_COUNT files..."
        fi
    fi
done

echo ""
echo "✅ Successfully copied $COPIED MP3 files to $SOUNDS_DEST"
echo ""
echo "SD card is ready! Insert it into the Korvo1 device."
echo "The device will automatically detect files at /sdcard/sounds/"
