# Full TFLite Training Status

## ✅ What's Set Up

1. **Training Script:** `train_full_hey_nap.py`
   - Wrapper for OpenWakeWord training pipeline
   - Handles all training steps

2. **Training Config:** `training_config_hey_nap.yaml`
   - Configuration for "Hey Nap" model
   - Ready to customize

3. **Dependencies Installed:**
   - ✅ PyTorch
   - ✅ openwakeword
   - ✅ numpy, scipy, sklearn
   - ✅ ONNX to TFLite conversion tools

4. **Setup Script:** `setup_full_training.sh`
   - Installs all required dependencies

## ⚠️ What's Still Needed

### Required for Full Training:

1. **TTS Models (Piper)** - For generating ~100,000 synthetic training samples
   - Install: https://github.com/rhasspy/piper
   - Update `piper_sample_generator_path` in `training_config_hey_nap.yaml`
   - This is required for the `--generate-clips` step

2. **Validation Data** - For model evaluation
   - Path to false positive validation dataset
   - Update `false_positive_validation_data_path` in config
   - Can use pre-computed features or generate your own

### Optional (but recommended):

3. **Negative Datasets** - For better model performance
   - Background audio datasets
   - Room impulse response (RIR) datasets
   - Update `background_paths` and `rir_paths` in config

## 🚀 How to Train

### Option 1: Full Local Training (Complex)

**Step 1: Configure**
```bash
# Edit training_config_hey_nap.yaml
# - Set piper_sample_generator_path
# - Set false_positive_validation_data_path
# - Optionally add background_paths and rir_paths
```

**Step 2: Generate Training Data**
```bash
python3 train_full_hey_nap.py --generate-clips
```
This will:
- Generate ~100,000 synthetic "Hey Nap" samples using TTS
- Generate adversarial negative samples
- Takes time (depends on TTS speed)

**Step 3: Augment Data**
```bash
python3 train_full_hey_nap.py --augment-clips
```
This will:
- Add background noise
- Apply room impulse responses
- Compute OpenWakeWord features

**Step 4: Train Model**
```bash
python3 train_full_hey_nap.py --train-model
```
This will:
- Train the model (takes hours, faster with GPU)
- Export to ONNX
- Convert to TFLite

**Or run all steps:**
```bash
python3 train_full_hey_nap.py --all
```

### Option 2: Google Colab (⭐ Recommended - Much Easier)

**Why Colab is better:**
- ✅ No setup required
- ✅ Free GPU access
- ✅ TTS models pre-configured
- ✅ All dependencies included
- ✅ Automated pipeline
- ✅ ~1 hour total time

**Steps:**
1. Open: https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb
2. Set wake word: `"hey nap"`
3. Run all cells
4. Download `hey_nap.tflite`

## 📊 Current Status

- ✅ Dependencies installed
- ✅ Training scripts ready
- ✅ Config file ready
- ⚠️ TTS models needed (Piper)
- ⚠️ Validation data needed
- ⚠️ Optional: Negative datasets

## 🎯 Recommended Path

**For fastest results:** Use Google Colab notebook
- No additional setup
- Everything pre-configured
- ~1 hour to complete

**For local training:** 
1. Install Piper TTS
2. Configure training_config_hey_nap.yaml
3. Run training steps
4. Expect several hours of training time

## 📝 Next Steps

1. **If using Colab:**
   - Open the notebook
   - Train the model
   - Download TFLite file
   - Test: `python3 test_hey_nap_local.py`

2. **If training locally:**
   - Install Piper TTS
   - Configure `training_config_hey_nap.yaml`
   - Run: `python3 train_full_hey_nap.py --all`
   - Wait for training to complete
   - Test: `python3 test_hey_nap_local.py --model models/hey_nap/hey_nap.tflite`

## 📁 Files Created

- `train_full_hey_nap.py` - Main training script
- `training_config_hey_nap.yaml` - Training configuration
- `setup_full_training.sh` - Dependency installer
- `test_hey_nap_local.py` - Test script (already exists)

## 💡 Tips

- GPU significantly speeds up training (10x+ faster)
- Training on CPU will take many hours
- Colab provides free GPU access
- Model quality improves with more training data
- Full pipeline generates ~100k samples automatically
