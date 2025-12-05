# Colab Training Fix - Missing Config Field

## Issue

The training failed because the config file was missing the required `batch_n_per_class` field.

## Fix Applied

Updated `training_config_hey_nap_improved.yaml` to include:
```yaml
batch_n_per_class: 8  # Required field for training
```

## Updated Notebook

The Colab notebook now:
1. ✅ Includes `batch_n_per_class: 8` in the config
2. ✅ Better error checking and reporting
3. ✅ Shows directory contents if model not found
4. ✅ More helpful error messages

## How to Use

1. **Re-run the config cell** in Colab to regenerate the config with the fix
2. **Re-run the training cell** - it should work now
3. **Check the output** - the notebook will show what files were created

## If Training Still Fails

Check for these common issues:

1. **Piper TTS not cloned:**
   - Make sure the cell that clones `piper_sample_generator` ran successfully
   - Should see: `✅ Piper setup complete`

2. **GPU out of memory:**
   - Try reducing `n_samples` from 200000 to 100000
   - Or reduce `batch_n_per_class` from 8 to 4

3. **Missing dependencies:**
   - Re-run the first cell to install dependencies

4. **Check full error:**
   - Scroll up in the training cell output
   - Look for the actual Python error message

## Alternative: Use Official Notebook

If you continue having issues, use the official OpenWakeWord Colab notebook:

**Link:** https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb

Just change the wake word to `"hey nap"` and it will work automatically.
