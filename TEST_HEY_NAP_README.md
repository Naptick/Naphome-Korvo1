# Testing "Hey Nap" Wake Word Locally on macOS

This guide explains how to test the "Hey Nap" wake word detection locally on macOS using the same model format and processing approach as the ESP32-S3 Korvo1.

## Quick Start

1. **Setup the test environment:**
   ```bash
   ./setup_test_environment.sh
   ```

2. **Run the test:**
   ```bash
   python3 test_hey_nap_local.py
   ```

## What This Tests

The test script (`test_hey_nap_local.py`) simulates the ESP32-S3 Korvo1 wake word detection:

- ✅ **TFLite Models**: Uses TFLite format (same as ESP32)
- ✅ **Chunking**: Processes audio in 512-sample chunks (32ms at 16kHz), matching ESP32
- ✅ **Buffering**: Accumulates chunks to 1280 samples for OpenWakeWord inference
- ✅ **Same Processing**: Mirrors ESP32's audio processing pipeline

## Requirements

### Python Dependencies

For ESP32 compatibility, you need TFLite runtime:

```bash
pip install tflite-runtime openwakeword numpy
```

Or use the setup script:
```bash
./setup_test_environment.sh
```

### Audio Format

- Sample rate: 16 kHz
- Bit depth: 16-bit
- Channels: Mono
- Format: WAV

The script automatically generates test audio using macOS `say` command.

## Usage

### Basic Test

```bash
python3 test_hey_nap_local.py
```

This will:
1. Generate "Hey Nap" audio using macOS `say` command
2. Test detection with available models
3. Show detection results and scores

### Test with Custom Model

If you have a custom `hey_nap.tflite` model:

```bash
# Place model in project root or models/ directory
python3 test_hey_nap_local.py

# Or specify model path explicitly
python3 test_hey_nap_local.py --model path/to/hey_nap.tflite
```

### Test with Existing WAV File

```bash
python3 test_hey_nap_local.py --wav my_audio.wav
```

### Adjust Detection Threshold

```bash
python3 test_hey_nap_local.py --threshold 0.3
```

Default threshold is 0.5. Lower values = more sensitive (more false positives), higher values = less sensitive (more false negatives).

## Model Locations

The script searches for `hey_nap.tflite` in this order:

1. `hey_nap.tflite` (project root)
2. `models/hey_nap.tflite`
3. `components/openwakeword/models/hey_nap.tflite`
4. `components/openwakeword/lib/openwakeword/resources/models/hey_nap.tflite`

## Training a Custom "Hey Nap" Model

Currently, there's no pre-trained "Hey Nap" model. You'll need to train one:

### Option 1: Using OpenWakeWord Training

See: `components/openwakeword/lib/README.md`

Basic steps:
1. Collect audio samples (16kHz, 16-bit, mono WAV):
   - 3+ examples of "Hey Nap" (positive)
   - 10+ seconds of other speech (negative)
2. Train using OpenWakeWord's training script
3. Export as TFLite format
4. Place in one of the model locations above

### Option 2: Custom Verifier Model

For speaker-specific verification (reduces false positives):

```python
from openwakeword import train_custom_verifier

train_custom_verifier(
    positive_reference_clips=['hey_nap1.wav', 'hey_nap2.wav', 'hey_nap3.wav'],
    negative_reference_clips=['other1.wav', 'other2.wav'],
    output_path='hey_nap_verifier.pkl',
    model_name='hey_jarvis'  # base model
)
```

## Baseline Testing

If you don't have a custom "Hey Nap" model yet, the script will use `hey_jarvis` as a baseline. This won't detect "Hey Nap" correctly, but it verifies:

- ✅ Audio generation works
- ✅ Model loading works
- ✅ Processing pipeline works
- ✅ Detection framework is functional

## ESP32 Deployment

For ESP32 deployment:

1. **Models should be in TFLite format** (`.tflite`)
2. **Place models in**: `components/openwakeword/models/`
3. **Download models**: 
   ```bash
   cd components/openwakeword
   ./download_models.sh
   ```
4. **See integration guide**: `components/openwakeword/INTEGRATION.md`

## Troubleshooting

### "TFLite runtime not found"

Install TFLite runtime:
```bash
pip install tflite-runtime
```

### "No custom 'hey_nap' model found"

This is expected if you haven't trained a custom model yet. The script will use `hey_jarvis` as a baseline.

### "No detections found"

- If using `hey_jarvis` model: This is expected - "Hey Nap" won't match "Hey Jarvis"
- If using custom model: Try lowering the threshold (`--threshold 0.3`)
- Check audio quality and format (16kHz, 16-bit, mono)

### Model path issues

Make sure your custom model is:
- Named `hey_nap.tflite` (or specify with `--model`)
- In one of the search locations listed above
- Valid TFLite format

## Files

- `test_hey_nap_local.py` - Main test script
- `setup_test_environment.sh` - Setup script for dependencies
- `components/openwakeword/download_models.sh` - Download ESP32 models
- `components/openwakeword/INTEGRATION.md` - ESP32 integration guide

## Next Steps

1. ✅ Test with baseline model (`hey_jarvis`) to verify pipeline
2. 📝 Train custom "Hey Nap" model
3. 🧪 Test with custom model locally
4. 🚀 Deploy to ESP32-S3 Korvo1

For ESP32 deployment details, see: `components/openwakeword/INTEGRATION.md`
