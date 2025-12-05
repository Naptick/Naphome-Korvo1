# Training "Hey Nap" TFLite Model - Summary

## ✅ What We've Set Up

1. **Training script:** `train_hey_nap_model.py`
   - Can generate training data using macOS `say` command
   - Can train a custom verifier model (simpler approach)

2. **Test script:** `test_hey_nap_local.py`
   - Tests models with ESP32-compatible processing
   - Automatically finds and uses custom models

3. **Training data generated:** `training_data/positive/`
   - 20 samples of "Hey Nap" ready for training

## 🎯 Goal: Standalone TFLite Model for ESP32

For a **standalone TFLite model** (no base model dependency), you have these options:

### Option 1: Google Colab (⭐ RECOMMENDED - Easiest)

**This is the best option for getting a TFLite model quickly.**

1. **Open the Colab notebook:**
   ```
   https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb
   ```

2. **In the notebook:**
   - Enter wake word: `"hey nap"` or `["hey nap"]`
   - Run all cells
   - The notebook will:
     - Generate ~100,000 synthetic training samples using TTS
     - Train the model
     - Export as both ONNX and TFLite

3. **Download the model:**
   - Download `hey_nap.tflite` from Colab
   - Save to: `hey_nap.tflite` in project root

4. **Test it:**
   ```bash
   python3 test_hey_nap_local.py
   ```

**Time:** ~1 hour  
**Difficulty:** Easy  
**Result:** Full standalone TFLite model ✅

### Option 2: Local Full Training (Advanced)

**Requires:** PyTorch, GPU, TTS models, large datasets

See: `TRAIN_MODEL_README.md` for detailed instructions.

**Time:** Several hours + setup time  
**Difficulty:** Hard  
**Result:** Full standalone TFLite model ✅

### Option 3: Custom Verifier Model (Simpler, but Limited)

**What it does:**
- Creates a `.pkl` verifier model
- Works with base model (like `hey_jarvis`)
- Good for speaker-specific verification

**Limitations:**
- ❌ Not a standalone TFLite model
- ❌ Requires base model to function
- ❌ Can't be directly used on ESP32 without base model

**To train:**
```bash
python3 train_hey_nap_model.py --train-verifier
```

**Time:** ~10 minutes  
**Difficulty:** Easy  
**Result:** Verifier model (not standalone TFLite) ⚠️

## 📋 Recommended Path

**For ESP32 deployment, use Option 1 (Google Colab):**

1. ✅ Use Colab notebook to train full TFLite model
2. ✅ Download `hey_nap.tflite`
3. ✅ Test locally: `python3 test_hey_nap_local.py`
4. ✅ Deploy to ESP32: Place in `components/openwakeword/models/`

## 🧪 Testing Your Model

Once you have `hey_nap.tflite`:

```bash
# Test with the model
python3 test_hey_nap_local.py

# Or specify model path
python3 test_hey_nap_local.py --model hey_nap.tflite
```

The test script will:
- ✅ Use ESP32-compatible processing (512-sample chunks)
- ✅ Show detection results
- ✅ Verify the model works before ESP32 deployment

## 📁 Files Created

- `train_hey_nap_model.py` - Training script
- `test_hey_nap_local.py` - Test script (ESP32-compatible)
- `training_data/positive/` - Generated training samples
- `TRAIN_MODEL_README.md` - Detailed training guide
- `TEST_HEY_NAP_README.md` - Testing guide

## 🚀 Next Steps

1. **Train the model:**
   - Use Google Colab (easiest): https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb
   - Or follow local training guide: `TRAIN_MODEL_README.md`

2. **Test locally:**
   ```bash
   python3 test_hey_nap_local.py
   ```

3. **Deploy to ESP32:**
   - Copy `hey_nap.tflite` to `components/openwakeword/models/`
   - Update ESP32 code to load the model
   - Build and flash

## 📚 Resources

- **Colab Notebook:** https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb
- **OpenWakeWord Docs:** `components/openwakeword/lib/README.md`
- **ESP32 Integration:** `components/openwakeword/INTEGRATION.md`
