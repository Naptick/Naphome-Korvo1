# Improve "Hey Nap" Model - Complete Guide

## Current Model Status

- **Max Score:** 0.018431 (target: >0.5)
- **Status:** Functional but needs improvement
- **Issue:** Low detection scores indicate insufficient training data/quality

## Quick Start: Retrain in Colab

### Step 1: Use Improved Colab Notebook

1. **Open:** `train_hey_nap_improved_colab.ipynb` in Google Colab
   - Or use: https://colab.research.google.com/
   - Upload the notebook

2. **The notebook includes:**
   - Improved model architecture (256 units, 2 blocks)
   - More training data (200k samples)
   - Longer training (20k steps)
   - Better augmentation
   - Adversarial negative examples

3. **Run all cells** - Training takes 1-2 hours

4. **Download ONNX model** when complete

### Step 2: Convert to TFLite

```bash
# Use the working Docker pipeline
./convert_with_docker.sh
```

### Step 3: Test Improved Model

```bash
python3 test_hey_nap_local.py --model hey_nap.tflite
```

**Expected:** Max score should be >0.5 (vs current 0.018)

## Detailed Improvement Strategy

### 1. Collect Real Training Data ⭐ **Most Important**

**Why:** Real audio is better than synthetic TTS

**How to Collect:**

```bash
# Use the data collection script
python3 collect_training_data.py --samples 50 --speaker speaker1
```

**Target:**
- 50+ samples per speaker
- 5-10 different speakers
- Multiple environments (quiet, noisy, echoey)
- Various speaking styles (normal, whisper, loud, fast, slow)

**Tips:**
- Record in the same environment as deployment (if possible)
- Use the same microphone type
- Natural speech (not too fast/slow)
- Clear pronunciation

### 2. Use Improved Training Config

**File:** `training_config_hey_nap_improved.yaml`

**Key Improvements:**
- **Model Size:** 256 units, 2 blocks (vs 128, 1 block)
- **Training Data:** 200k samples (vs 100k)
- **Training Steps:** 20k (vs 10k)
- **Augmentation:** 2 rounds (vs 1)
- **Negative Examples:** Similar-sounding phrases added

### 3. Training Process

**Option A: Colab (Recommended)**
1. Upload `train_hey_nap_improved_colab.ipynb`
2. Optionally upload real training data
3. Run all cells
4. Download ONNX model
5. Convert with Docker

**Option B: Local (Advanced)**
```bash
# Use improved config
python3 train_full_hey_nap.py \
  --config training_config_hey_nap_improved.yaml \
  --all
```

### 4. Compare Results

**Before (Current Model):**
- Max score: 0.018431
- Mean score: 0.002212
- Detection rate: 0% (threshold 0.5)

**After (Improved Model):**
- Target max score: >0.5
- Target detection rate: >80%
- Target false positive rate: <5%

## Training Configuration Comparison

| Parameter | Original | Improved | Benefit |
|-----------|----------|----------|---------|
| Layer Size | 128 | 256 | More capacity |
| Blocks | 1 | 2 | Deeper network |
| Training Samples | 100k | 200k | Better generalization |
| Training Steps | 10k | 20k | More convergence |
| Augmentation Rounds | 1 | 2 | More robustness |
| Negative Phrases | 0 | 13 | Fewer false positives |

## Expected Timeline

- **Data Collection:** 1-2 hours (50 samples × 5 speakers)
- **Colab Training:** 1-2 hours (with GPU)
- **Conversion:** 2-3 minutes (Docker)
- **Testing:** 5 minutes
- **Total:** ~3-4 hours

## Success Criteria

After improvement, you should see:

✅ **Max score > 0.5** (currently 0.018)  
✅ **Detection rate > 80%** on test audio  
✅ **False positive rate < 5%**  
✅ **Consistent scores** across different speakers  
✅ **Robust to background noise**

## Troubleshooting

**If scores are still low after retraining:**

1. **Check training data quality**
   - Are samples clear?
   - Do they match deployment environment?
   - Enough diversity?

2. **Increase training further**
   - Try 300k samples
   - Try 30k steps
   - Try 3 augmentation rounds

3. **Add more real data**
   - Collect 100+ samples
   - More speakers
   - More environments

4. **Fine-tune existing model**
   - Load current model
   - Continue training with new data
   - Lower learning rate (0.0001)

## Files Created

- ✅ `training_config_hey_nap_improved.yaml` - Improved config
- ✅ `train_hey_nap_improved_colab.ipynb` - Improved Colab notebook
- ✅ `collect_training_data.py` - Data collection script
- ✅ `IMPROVE_MODEL_GUIDE.md` - This guide

## Next Steps

1. **Collect real data** (optional but recommended)
2. **Retrain in Colab** with improved config
3. **Convert to TFLite** using Docker
4. **Test and compare** scores
5. **Deploy improved model** to ESP32

---

**Ready to improve!** Start with the Colab notebook for the fastest results.
