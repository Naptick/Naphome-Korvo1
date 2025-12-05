# Colab Quick Reference - "Hey Nap" Training

## 🎯 Quick Checklist

- [ ] Opened Colab notebook
- [ ] Enabled GPU (Runtime → Change runtime type → GPU)
- [ ] Found wake word configuration cell
- [ ] Changed wake word to `"hey nap"`
- [ ] Running all cells
- [ ] Waiting for training to complete (~1 hour)
- [ ] Downloading `hey_nap.tflite` when done

## 📍 Finding the Wake Word Configuration

Look for a cell that contains something like:

```python
wake_word = "hey jarvis"
```

or

```python
wake_word = ["hey jarvis"]
```

**Change it to:**
```python
wake_word = "hey nap"
```

or

```python
wake_word = ["hey nap"]
```

## ⚡ Quick Commands

### Enable GPU
1. **Runtime** → **Change runtime type**
2. Select **GPU** (T4)
3. Click **Save**

### Run All Cells
- **Runtime** → **Run all**
- Or: **Ctrl+F9** (Windows) / **Cmd+F9** (Mac)

### Check Progress
- Watch the progress bars in each cell
- Training cell will show: "Training step X/Y"

## 📥 Downloading the Model

After training completes, look for:

1. **Download link** at the bottom of the notebook
2. Or a cell with: `files.download('hey_nap.tflite')`
3. Or check the file browser (left sidebar) → `hey_nap.tflite`

**Save it to:** Your project directory as `hey_nap.tflite`

## 🧪 After Downloading

1. **Move to project:**
   ```bash
   # If downloaded to Downloads
   mv ~/Downloads/hey_nap.tflite .
   
   # Or place in models directory
   mv ~/Downloads/hey_nap.tflite models/
   ```

2. **Test the model:**
   ```bash
   python3 test_hey_nap_local.py
   ```

3. **Deploy to ESP32:**
   ```bash
   cp hey_nap.tflite components/openwakeword/models/
   ```

## ⏱️ Timeline

- **Setup:** 2 minutes
- **Training:** ~1 hour (automated)
- **Download:** 1 minute
- **Total:** ~1 hour

## 🐛 Common Issues

### "GPU not available"
- Wait a few minutes and try again
- Colab may be out of free GPUs
- Training will work on CPU (just slower)

### "Cell failed"
- Check the error message
- Try restarting runtime: **Runtime** → **Restart runtime**
- Then run all cells again

### "Can't find wake word cell"
- Look for cells with "wake_word" or "target_phrase"
- It's usually near the top, after imports
- Search: **Ctrl+F** (Windows) / **Cmd+F** (Mac) → search "wake"

### "Training seems stuck"
- Training takes ~1 hour
- Check the progress bar
- If no progress for 30+ minutes, restart

## ✅ Success Indicators

- ✅ All cells run without errors
- ✅ Training progress bars showing
- ✅ Final cell shows "Model saved" or similar
- ✅ Download link appears
- ✅ File `hey_nap.tflite` is downloadable

## 📞 Need Help?

If you're stuck:
1. Check error messages in the cell output
2. Restart runtime and try again
3. Make sure wake word is set correctly: `"hey nap"`
4. Ensure GPU is enabled

## 🎉 When Done

You'll have:
- ✅ `hey_nap.tflite` - Your trained model
- ✅ Ready to test locally
- ✅ Ready to deploy to ESP32

**Next:** Test with `python3 test_hey_nap_local.py`
