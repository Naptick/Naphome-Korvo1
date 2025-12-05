# Quick Start: Train "Hey Nap" TFLite Model

## Why Verifier Model Won't Work

The custom verifier model approach requires that the base model (like `hey_jarvis`) can detect your wake word first. Since "Hey Nap" won't be detected by "Hey Jarvis", we need a **full standalone model**.

## ✅ Best Solution: Google Colab Notebook

The easiest and fastest way to get a TFLite model is using the Google Colab notebook:

### Step 1: Open Colab Notebook

**Link:** https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb

Or find it in: `components/openwakeword/lib/README.md` under "Training New Models"

### Step 2: Configure for "Hey Nap"

In the notebook, look for the wake word configuration and set:

```python
wake_word = "hey nap"  # or ["hey nap"]
```

### Step 3: Run All Cells

The notebook will:
1. ✅ Generate ~100,000 synthetic training samples using TTS
2. ✅ Train the model automatically
3. ✅ Export as both ONNX and **TFLite** formats
4. ✅ Provide download links

**Time:** ~1 hour (mostly automated)

### Step 4: Download Model

1. Download `hey_nap.tflite` from Colab
2. Save to your project root: `hey_nap.tflite`

### Step 5: Test Locally

```bash
python3 test_hey_nap_local.py
```

The test script will automatically find and use your model!

### Step 6: Deploy to ESP32

```bash
# Copy to ESP32 models directory
cp hey_nap.tflite components/openwakeword/models/

# Update ESP32 code to load the model
# See: components/openwakeword/INTEGRATION.md
```

## Alternative: Local Full Training

If you prefer local training, you'll need:

**Requirements:**
- PyTorch with GPU
- TTS models (Piper) for synthetic data
- Large negative datasets (~30,000 hours)
- Room impulse response datasets
- Background audio datasets

**Steps:**

1. **Install dependencies:**
   ```bash
   pip install torch torchvision torchaudio
   pip install openwakeword
   ```

2. **Set up TTS generation:**
   - Install Piper TTS: https://github.com/rhasspy/piper
   - Configure path in training config

3. **Create training config** (`training_config.yaml`):
   ```yaml
   target_phrase: ["hey nap"]
   model_name: "hey_nap"
   n_samples: 100000
   n_samples_val: 10000
   output_dir: "./models"
   piper_sample_generator_path: "/path/to/piper"
   rir_paths: ["/path/to/rir/dataset"]
   background_paths: ["/path/to/background/audio"]
   background_paths_duplication_rate: [1]
   steps: 10000
   max_negative_weight: 10
   target_false_positives_per_hour: 0.5
   tts_batch_size: 16
   ```

4. **Run training:**
   ```bash
   cd components/openwakeword/lib
   python3 -m openwakeword.train \
     --training_config ../../training_config.yaml \
     --generate_clips \
     --augment_clips \
     --train_model
   ```

5. **Model will be exported to:**
   - `models/hey_nap/hey_nap.tflite`

**Time:** Several hours + setup time  
**Difficulty:** Advanced

## Why Colab is Recommended

- ✅ No setup required (everything pre-configured)
- ✅ Free GPU access
- ✅ All dependencies included
- ✅ Automated training pipeline
- ✅ Direct TFLite export
- ✅ ~1 hour total time

## What We Have Ready

1. ✅ **Test script:** `test_hey_nap_local.py`
   - ESP32-compatible testing
   - Automatically finds custom models
   - Ready to test your TFLite model

2. ✅ **Training data samples:** `training_data/positive/`
   - 20 "Hey Nap" samples (for reference)
   - Can be used for testing, but Colab generates better synthetic data

3. ✅ **Documentation:**
   - `TRAINING_SUMMARY.md` - Overview
   - `TRAIN_MODEL_README.md` - Detailed guide
   - `TEST_HEY_NAP_README.md` - Testing guide

## Next Steps

1. **Open Colab notebook:** https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb
2. **Train the model** (follow steps above)
3. **Download `hey_nap.tflite`**
4. **Test:** `python3 test_hey_nap_local.py`
5. **Deploy to ESP32**

## Troubleshooting

### Colab Issues
- Make sure GPU is enabled: Runtime → Change runtime type → GPU
- If cells fail, restart runtime and run again
- Check that wake word is set correctly: `"hey nap"`

### Testing Issues
- Ensure model is named `hey_nap.tflite`
- Place in project root or `models/` directory
- Check audio format: 16kHz, 16-bit, mono WAV

### ESP32 Deployment
- See: `components/openwakeword/INTEGRATION.md`
- Model must be in TFLite format
- Place in: `components/openwakeword/models/`

## Summary

**For a standalone TFLite model:**
- ✅ Use Google Colab (easiest, ~1 hour)
- ⚠️ Or local training (complex, several hours)

**The Colab notebook is the recommended path!**
