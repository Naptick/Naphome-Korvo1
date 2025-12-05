# Training "Hey Nap" Wake Word Model

This guide explains how to train a custom "Hey Nap" wake word model for ESP32-S3 Korvo1.

## Quick Start (Simplified Approach)

The simplest approach is to use a **custom verifier model** that works with a base model:

```bash
# 1. Generate training data
python3 train_hey_nap_model.py --generate-data

# 2. Train verifier model
python3 train_hey_nap_model.py --train-verifier

# 3. Test the model
python3 test_hey_nap_local.py
```

**Note:** This creates a verifier model (`.pkl`) that works with a base model like `hey_jarvis`. It's simpler but requires the base model.

## Full TFLite Model Training

For a **standalone TFLite model** (no base model required), you have two options:

### Option 1: Google Colab (Recommended - Easiest)

The easiest way to train a full model is using the Google Colab notebook:

1. **Open the Colab notebook:**
   - https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb
   - Or find it in: `components/openwakeword/lib/README.md`

2. **Follow the notebook steps:**
   - Enter "Hey Nap" as your wake word
   - The notebook will generate synthetic training data
   - Train the model
   - Export as TFLite

3. **Download the model:**
   - Save `hey_nap.tflite` to your project
   - Place in: `hey_nap.tflite` or `models/hey_nap.tflite`

4. **Test locally:**
   ```bash
   python3 test_hey_nap_local.py
   ```

### Option 2: Local Training (Advanced)

Full local training requires significant setup:

**Requirements:**
- PyTorch with GPU support
- TTS models (Piper) for synthetic data generation
- Large negative datasets (~30,000 hours)
- Room impulse response (RIR) datasets
- Background audio datasets

**Steps:**

1. **Set up TTS generation:**
   - Install Piper TTS: https://github.com/rhasspy/piper
   - Configure path in training config

2. **Prepare datasets:**
   - Download negative audio datasets
   - Download RIR datasets
   - Download background audio

3. **Create training config:**
   ```yaml
   target_phrase: ["hey nap"]
   model_name: "hey_nap"
   n_samples: 100000
   # ... (see train.py for full config)
   ```

4. **Run training:**
   ```bash
   cd components/openwakeword/lib
   python3 -m openwakeword.train \
     --training_config config.yaml \
     --generate_clips \
     --augment_clips \
     --train_model
   ```

5. **Export to TFLite:**
   - Training automatically exports ONNX and TFLite
   - Model will be in: `output_dir/hey_nap/hey_nap.tflite`

**See:** `components/openwakeword/lib/README.md` for detailed instructions.

## Verifier Model vs Full Model

### Custom Verifier Model (Simpler)
- ✅ Easy to train (just need a few audio samples)
- ✅ Works with base model (like `hey_jarvis`)
- ✅ Good for speaker-specific verification
- ❌ Requires base model to function
- ❌ Creates `.pkl` file (not standalone TFLite)

**Use when:** You want quick results and can use a base model.

### Full TFLite Model (Standalone)
- ✅ Standalone model (no base model needed)
- ✅ Native TFLite format for ESP32
- ✅ Better performance potential
- ❌ Complex training setup
- ❌ Requires GPU and large datasets
- ❌ More time-consuming

**Use when:** You need a standalone model or maximum performance.

## Training Data Requirements

### For Verifier Model (Simple)
- **Positive samples:** 3-10 examples of "Hey Nap" (16kHz, 16-bit, mono WAV)
- **Negative samples:** 5-20 examples of other speech (16kHz, 16-bit, mono WAV)

### For Full Model (Advanced)
- **Positive samples:** ~100,000+ synthetic examples (generated via TTS)
- **Negative samples:** ~30,000 hours of diverse audio (speech, noise, music)

## Testing Your Model

After training, test your model:

```bash
# Test with custom model
python3 test_hey_nap_local.py --model path/to/hey_nap.tflite

# Or if model is in standard location
python3 test_hey_nap_local.py
```

## ESP32 Deployment

Once you have a TFLite model:

1. **Place model in ESP32 directory:**
   ```bash
   cp hey_nap.tflite components/openwakeword/models/
   ```

2. **Update ESP32 code:**
   - Modify `openwakeword_esp32.cpp` to load your model
   - See: `components/openwakeword/INTEGRATION.md`

3. **Build and flash:**
   ```bash
   idf.py build flash
   ```

## Troubleshooting

### "Not enough training data"
- Generate more samples: `--positive-count 100`
- Use the Colab notebook for synthetic data generation

### "Training fails"
- Check audio format: 16kHz, 16-bit, mono WAV
- Ensure openwakeword is installed: `pip install openwakeword`
- For full training, ensure PyTorch and GPU are available

### "Model doesn't detect well"
- Train with more diverse samples
- Adjust detection threshold: `--threshold 0.3`
- Use full training pipeline for better results

## Resources

- **OpenWakeWord Docs:** `components/openwakeword/lib/README.md`
- **Training Code:** `components/openwakeword/lib/openwakeword/train.py`
- **Google Colab:** https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb
- **ESP32 Integration:** `components/openwakeword/INTEGRATION.md`
