# Quick Fix for Colab - Run This Cell

## The Problem
The conversion failed because `onnx` module wasn't available when `onnx2tf` tried to run.

## Quick Fix

**In your Colab notebook, replace the conversion cell (Step 3) with this:**

```python
from pathlib import Path
import os

# Get model name from file
model_name = Path(onnx_file).stem
tflite_file = f"{model_name}.tflite"

print(f"🔄 Converting {onnx_file} to TFLite...")
print(f"   Output will be: {tflite_file}")
print("\nEnsuring dependencies are installed...")
!pip install -q onnx onnx2tf
print("\nThis may take a minute...")

# Convert using onnx2tf
!onnx2tf -i {onnx_file} -o . -osd

# Check for output file (onnx2tf creates _float32.tflite)
output_file = f"{model_name}_float32.tflite"

if os.path.exists(output_file):
    # Rename to desired name
    os.rename(output_file, tflite_file)
    print(f"\n✅ Conversion successful!")
    print(f"   TFLite model: {tflite_file}")
    print(f"   File size: {os.path.getsize(tflite_file) / 1024:.1f} KB")
else:
    # Check if any TFLite file was created
    import glob
    tflite_files = glob.glob("*.tflite")
    if tflite_files:
        print(f"\n⚠️  Found TFLite file: {tflite_files[0]}")
        if tflite_files[0] != tflite_file:
            os.rename(tflite_files[0], tflite_file)
        print(f"✅ Renamed to: {tflite_file}")
    else:
        print("\n❌ TFLite file not found")
        print("\nTrying alternative method (see troubleshooting cell below)...")
        print("Available files:")
        !ls -la
```

**Or just add this line at the start of your conversion cell:**

```python
!pip install -q onnx onnx2tf
```

Then run the conversion again.

## Updated Notebook

I've updated `convert_hey_nap_to_tflite.ipynb` with the fix. You can:
1. Re-upload the updated notebook, OR  
2. Just add `!pip install -q onnx onnx2tf` at the start of your conversion cell

The fix ensures `onnx` is installed right before conversion.
