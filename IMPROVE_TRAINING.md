# Improve "Hey Nap" Model Training

## Current Status

- ✅ Model converted to TFLite successfully
- ✅ Model loads and runs
- ⚠️  Low detection scores (max: 0.018431, target: >0.5)
- 📊 Model needs better training data and/or more training

## Why Scores Are Low

1. **Limited Training Data** - Model may need more diverse examples
2. **Audio Quality Mismatch** - Test audio may differ from training data
3. **Model Architecture** - May need tuning for "Hey Nap" specifically
4. **Training Duration** - May need more epochs

## Training Improvement Strategies

### Option 1: More Training Data (Recommended)

**Add more diverse "Hey Nap" samples:**

1. **Record Real Audio**
   ```bash
   # Record multiple speakers saying "Hey Nap"
   # Different:
   - Speaking speeds (fast, slow, normal)
   - Voice tones (high, low, normal)
   - Background noise levels
   - Microphone positions
   - Room acoustics
   ```

2. **Generate More Synthetic Data**
   - Use different TTS voices
   - Vary speaking rate
   - Add different background noise
   - Use different audio effects

3. **Data Augmentation**
   - Pitch shifting
   - Time stretching
   - Noise addition
   - Reverb/echo
   - Volume variations

### Option 2: Retrain with Better Configuration

**Update training config:**

```yaml
# training_config_hey_nap.yaml improvements

# Increase training data
synthetic_data:
  num_samples: 10000  # Increase from default
  num_speakers: 50     # More speaker diversity
  
# More augmentation
augmentation:
  pitch_shift_range: [-3, 3]  # Wider range
  time_stretch_range: [0.9, 1.1]
  noise_level: 0.1  # Add more noise
  
# Longer training
training:
  epochs: 100  # More epochs
  batch_size: 32
  learning_rate: 0.001
```

### Option 3: Use Better Base Model

**Try different base models:**
- `hey_jarvis` (current)
- `alexa` (may work better for 2-syllable phrases)
- `hey_mycroft` (similar structure to "Hey Nap")

### Option 4: Fine-tune Existing Model

**Instead of training from scratch:**
1. Load your current `hey_nap.onnx` model
2. Continue training with more data
3. Use lower learning rate (0.0001)
4. Train for additional epochs

## Quick Training Improvement Steps

### Step 1: Collect More Data

```bash
# Record 50+ samples of "Hey Nap"
# Use different:
# - Speakers (5-10 people)
# - Environments (quiet, noisy, echoey)
# - Speaking styles (whisper, normal, loud)
```

### Step 2: Retrain in Colab

1. **Upload new training data** to Colab
2. **Update config** with more samples
3. **Run training** with:
   - More epochs (50-100)
   - Better augmentation
   - More synthetic data

### Step 3: Test and Iterate

1. **Convert to TFLite** (use the working Docker method)
2. **Test locally** with `test_hey_nap_local.py`
3. **Check scores** - aim for max > 0.5
4. **Repeat** if needed

## Training Data Checklist

- [ ] 50+ real audio samples
- [ ] 5+ different speakers
- [ ] Multiple environments
- [ ] Various speaking styles
- [ ] Good quality recordings (16kHz, mono)
- [ ] Balanced positive/negative examples

## Expected Results After Improvement

- **Max score:** > 0.5 (currently 0.018)
- **Detection rate:** > 80% on test set
- **False positive rate:** < 5%
- **Consistent scores** across different speakers

## Quick Start: Retrain in Colab

1. **Go to:** https://colab.research.google.com/
2. **Upload:** `train_hey_nap_colab.ipynb` (or use official OpenWakeWord notebook)
3. **Add more training data** (upload WAV files)
4. **Update config** with more samples/epochs
5. **Train** (takes 1-2 hours)
6. **Convert** using the Docker method we fixed
7. **Test** and deploy

## Files Ready

- ✅ `hey_nap.tflite` - Current model (deployed)
- ✅ `convert_hey_nap_docker.py` - Working conversion script
- ✅ `Dockerfile.convert` - Working Docker setup
- ✅ `convert_with_docker.sh` - Easy conversion script

## Next Steps

1. **Collect more training data** (50+ samples)
2. **Retrain in Colab** with improved config
3. **Convert new model** using Docker
4. **Test and compare** scores
5. **Deploy improved model** to ESP32

The conversion pipeline is now working - focus on improving the training data and model quality!
