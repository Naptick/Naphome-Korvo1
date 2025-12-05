# Quick Start: Improve "Hey Nap" Model

## 🚀 Fastest Path to Better Model

### Step 1: Retrain in Colab (1-2 hours)

1. **Open Colab:** https://colab.research.google.com/
2. **Upload:** `train_hey_nap_improved_colab.ipynb`
3. **Run all cells** - Uses improved config automatically
4. **Download** the new ONNX model

### Step 2: Convert to TFLite (2 minutes)

```bash
./convert_with_docker.sh
```

### Step 3: Test (5 minutes)

```bash
python3 test_hey_nap_local.py --model hey_nap.tflite
```

**Expected:** Max score should improve from 0.018 to >0.5

## 📊 What's Improved

| Aspect | Original | Improved | Impact |
|--------|----------|----------|--------|
| Model Size | 128 units, 1 block | 256 units, 2 blocks | +100% capacity |
| Training Data | 100k samples | 200k samples | +100% data |
| Training Steps | 10k | 20k | +100% training |
| Augmentation | 1 round | 2 rounds | More robust |
| Negative Examples | 0 | 13 phrases | Fewer false positives |

## 📁 Files Created

- ✅ `train_hey_nap_improved_colab.ipynb` - Ready-to-use Colab notebook
- ✅ `training_config_hey_nap_improved.yaml` - Improved config
- ✅ `collect_training_data.py` - Optional: collect real audio
- ✅ `IMPROVE_MODEL_GUIDE.md` - Complete guide

## ⏱️ Timeline

- **Colab Training:** 1-2 hours
- **Conversion:** 2-3 minutes  
- **Testing:** 5 minutes
- **Total:** ~2 hours

## 🎯 Success Criteria

After improvement:
- ✅ Max score > 0.5 (currently 0.018)
- ✅ Detection rate > 80%
- ✅ Ready for ESP32 deployment

---

**Start now:** Upload `train_hey_nap_improved_colab.ipynb` to Colab!
