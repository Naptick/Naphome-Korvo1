# Google Colab Setup for "Hey Nap" Training

I've created a simplified Colab notebook, but **the best approach is to use the official OpenWakeWord training notebook** which has everything pre-configured.

## 🚀 Quick Start (Recommended)

### Option 1: Use Official OpenWakeWord Notebook (Best)

1. **Open the notebook:**
   - Direct link: https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb
   - Or find it in: `components/openwakeword/lib/README.md`

2. **Enable GPU:**
   - Runtime → Change runtime type → GPU (T4)
   - This speeds up training significantly

3. **Configure wake word:**
   - Find the cell that sets the wake word
   - Change it to: `"hey nap"` or `["hey nap"]`
   - Example:
     ```python
     wake_word = "hey nap"
     ```

4. **Run all cells:**
   - Runtime → Run all
   - Or click the "Run all" button
   - This takes ~1 hour (mostly automated)

5. **Download the model:**
   - At the end, you'll see download links
   - Download `hey_nap.tflite`
   - Save it to your project

6. **Test locally:**
   ```bash
   python3 test_hey_nap_local.py
   ```

### Option 2: Use Simplified Notebook (I Created)

I've created `train_hey_nap_colab.ipynb` which you can upload to Colab, but it's a simplified version. The official notebook is better.

**To use it:**
1. Go to: https://colab.research.google.com/
2. File → Upload notebook
3. Upload `train_hey_nap_colab.ipynb`
4. Follow the cells

## 📋 Step-by-Step for Official Notebook

### Step 1: Open Notebook
- Link: https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb

### Step 2: Enable GPU
1. Click: **Runtime** → **Change runtime type**
2. Select: **GPU** (T4)
3. Click: **Save**

### Step 3: Find Wake Word Configuration
Look for a cell that looks like:
```python
wake_word = "hey jarvis"  # or similar
```

Change it to:
```python
wake_word = "hey nap"
```

### Step 4: Run All Cells
1. Click: **Runtime** → **Run all**
2. Or use: **Ctrl+F9** (Windows) / **Cmd+F9** (Mac)
3. Wait for completion (~1 hour)

### Step 5: Download Model
At the end, you'll see:
- Download links for the trained model
- Look for: `hey_nap.tflite`
- Click to download

### Step 6: Save to Project
```bash
# Move downloaded file to project
mv ~/Downloads/hey_nap.tflite .

# Or place in models directory
mv ~/Downloads/hey_nap.tflite models/
```

### Step 7: Test
```bash
python3 test_hey_nap_local.py
```

## 🎯 What the Notebook Does

The official notebook automatically:

1. ✅ **Installs dependencies** (PyTorch, OpenWakeWord, etc.)
2. ✅ **Downloads TTS models** (Piper) for synthetic data generation
3. ✅ **Generates ~100,000 training samples** of "hey nap"
4. ✅ **Generates adversarial negative samples**
5. ✅ **Augments data** (adds noise, reverb)
6. ✅ **Trains the model** (with GPU acceleration)
7. ✅ **Exports to TFLite** format
8. ✅ **Provides download links**

**All automated!** You just need to:
- Set the wake word
- Run all cells
- Download the model

## ⏱️ Timeline

- **Setup:** 2 minutes (enable GPU, set wake word)
- **Training:** ~1 hour (automated)
- **Download:** 1 minute
- **Total:** ~1 hour

## 📁 Files

- `train_hey_nap_colab.ipynb` - Simplified notebook I created (optional)
- `COLAB_SETUP.md` - This guide
- Official notebook: https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb

## 💡 Tips

1. **Use GPU:** Makes training 10x+ faster
2. **Be patient:** Training takes ~1 hour even with GPU
3. **Check progress:** Watch the progress bars in cells
4. **Save model:** Download immediately after training completes
5. **Test locally:** Always test before deploying to ESP32

## 🐛 Troubleshooting

### "GPU not available"
- Runtime → Change runtime type → GPU
- May need to wait if all GPUs are in use

### "Notebook not found"
- Use direct link: https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb
- Or search "openWakeWord training" in Colab

### "Training failed"
- Check that wake word is set correctly: `"hey nap"`
- Restart runtime and try again
- Check error messages in cells

### "Can't download model"
- Files are saved in Colab's file system
- Use the download links provided
- Or use: `files.download('hey_nap.tflite')`

## ✅ Success Checklist

- [ ] Opened official Colab notebook
- [ ] Enabled GPU runtime
- [ ] Set wake word to `"hey nap"`
- [ ] Ran all cells successfully
- [ ] Downloaded `hey_nap.tflite`
- [ ] Saved to project directory
- [ ] Tested locally: `python3 test_hey_nap_local.py`
- [ ] Model works correctly

## 🚀 Next Steps After Training

1. **Test the model:**
   ```bash
   python3 test_hey_nap_local.py
   ```

2. **Deploy to ESP32:**
   - Copy to: `components/openwakeword/models/hey_nap.tflite`
   - Update ESP32 code
   - Build and flash

3. **See:** `TEST_HEY_NAP_README.md` for testing details

---

**Ready to train?** Open the notebook and follow the steps above!

**Link:** https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb
