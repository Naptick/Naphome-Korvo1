# Fix TFLite Conversion Error in Colab

## ✅ Good News
Your training completed successfully! The model was trained and the ONNX file was created.

## ❌ The Problem
The TFLite conversion failed due to a dependency issue with `onnx_tf` and `onnx_graphsurgeon`.

## 🔧 Quick Fix

Add this cell **before** the training cell (or run it now to fix the conversion):

```python
# Fix TFLite conversion dependencies
!pip install -q onnx-tf==1.10.0
!pip install -q onnx2tf
```

Or use this alternative conversion method:

```python
# Alternative: Convert ONNX to TFLite using onnx2tf
import subprocess
import os

onnx_file = f"my_custom_model/hey_nap.onnx"
tflite_file = f"my_custom_model/hey_nap.tflite"

# Use onnx2tf command line tool
!onnx2tf -i {onnx_file} -o {os.path.dirname(tflite_file)} -osd

# Rename the output file
!mv {os.path.dirname(tflite_file)}/hey_nap_float32.tflite {tflite_file}
```

## 🚀 Complete Fix Cell

Run this cell in Colab to fix the conversion:

```python
# Install correct dependencies for TFLite conversion
!pip install -q onnx-tf==1.10.0

# Convert ONNX to TFLite
import os
from pathlib import Path

model_name = "hey_nap"
onnx_path = f"my_custom_model/{model_name}.onnx"
tflite_path = f"my_custom_model/{model_name}.tflite"

if os.path.exists(onnx_path):
    print(f"Converting {onnx_path} to TFLite...")
    
    # Try using onnx2tf
    try:
        !onnx2tf -i {onnx_path} -o my_custom_model -osd
        # Rename output
        output_file = f"my_custom_model/{model_name}_float32.tflite"
        if os.path.exists(output_file):
            os.rename(output_file, tflite_path)
            print(f"✅ TFLite model created: {tflite_path}")
        else:
            print("⚠️  Output file not found, checking directory...")
            !ls -la my_custom_model/
    except Exception as e:
        print(f"⚠️  onnx2tf failed: {e}")
        print("\n💡 Alternative: Use ONNX model directly or convert locally")
        
    # Download ONNX model (works as fallback)
    if os.path.exists(onnx_path):
        print(f"\n📥 Downloading ONNX model: {onnx_path}")
        from google.colab import files
        files.download(onnx_path)
        
    # Download TFLite if it exists
    if os.path.exists(tflite_path):
        print(f"\n📥 Downloading TFLite model: {tflite_path}")
        files.download(tflite_path)
else:
    print(f"❌ ONNX file not found: {onnx_path}")
    print("Available files:")
    !ls -la my_custom_model/
```

## 📥 Download What You Have

Even if TFLite conversion failed, you can:

1. **Download the ONNX model:**
   ```python
   from google.colab import files
   files.download("my_custom_model/hey_nap.onnx")
   ```

2. **Convert ONNX to TFLite locally:**
   ```bash
   # On your Mac
   pip install onnx-tf tensorflow
   # Then use the conversion script
   ```

## 🔄 Alternative: Manual Conversion

If the Colab conversion keeps failing, download the ONNX model and convert it locally:

1. **Download ONNX:**
   ```python
   from google.colab import files
   files.download("my_custom_model/hey_nap.onnx")
   ```

2. **Convert locally on your Mac:**
   ```bash
   # Install converter
   pip install onnx-tf tensorflow
   
   # Convert (I'll create a script for this)
   python3 convert_onnx_to_tflite.py hey_nap.onnx hey_nap.tflite
   ```

## ✅ Quick Solution (Run This in Colab)

Copy and paste this into a new cell in Colab:

```python
# Quick fix for TFLite conversion
import os
from google.colab import files

model_name = "hey_nap"
onnx_file = f"my_custom_model/{model_name}.onnx"

# Check if ONNX exists
if os.path.exists(onnx_file):
    print(f"✅ ONNX model found: {onnx_file}")
    print("Downloading ONNX model (you can convert to TFLite locally)...")
    files.download(onnx_file)
    
    # Try to convert
    try:
        !pip install -q onnx-tf==1.10.0
        !onnx2tf -i {onnx_file} -o my_custom_model -osd
        tflite_file = f"my_custom_model/{model_name}_float32.tflite"
        if os.path.exists(tflite_file):
            !mv {tflite_file} my_custom_model/{model_name}.tflite
            print(f"✅ TFLite conversion successful!")
            files.download(f"my_custom_model/{model_name}.tflite")
    except Exception as e:
        print(f"⚠️  TFLite conversion failed: {e}")
        print("💡 Use the ONNX model or convert locally")
else:
    print("❌ ONNX file not found")
    print("Available files:")
    !ls -la my_custom_model/
```

## 🎯 What to Do Right Now

1. **Run the fix cell above** in Colab
2. **Download the ONNX model** (it works fine)
3. **Either:**
   - Try the TFLite conversion fix
   - Or convert ONNX to TFLite locally on your Mac

The ONNX model will work, but for ESP32 you need TFLite. The conversion should work with the fix above.
