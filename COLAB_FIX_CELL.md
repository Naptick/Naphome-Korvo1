# Quick Fix for Colab Conversion

## The Problem
The conversion failed because `onnx` module wasn't installed.

## Quick Fix - Run This Cell in Colab

Copy and paste this into a **new cell** in your Colab notebook (before the conversion step):

```python
# Fix: Install all required dependencies
!pip install -q onnx onnx2tf tensorflow
print("✅ All dependencies installed")
```

Then run the conversion cell again.

## Or Use This Complete Conversion Cell

Replace the conversion cell with this:

```python
from pathlib import Path
import os

# Get model name from file
model_name = Path(onnx_file).stem
tflite_file = f"{model_name}.tflite"

# Install dependencies first
print("Installing dependencies...")
!pip install -q onnx onnx2tf tensorflow

print(f"\n🔄 Converting {onnx_file} to TFLite...")
print(f"   Output will be: {tflite_file}")
print("\nThis may take a minute...")

# Convert using onnx2tf
!onnx2tf -i {onnx_file} -o . -osd

# Check for output file
output_file = f"{model_name}_float32.tflite"

if os.path.exists(output_file):
    os.rename(output_file, tflite_file)
    print(f"\n✅ Conversion successful!")
    print(f"   TFLite model: {tflite_file}")
    print(f"   File size: {os.path.getsize(tflite_file) / 1024:.1f} KB")
else:
    print("\n⚠️  onnx2tf output not found, trying alternative method...")
    # The alternative method cell will handle this
```

## Updated Notebook

I've updated `convert_hey_nap_to_tflite.ipynb` to include the fix. You can:
1. Re-upload the updated notebook, OR
2. Just run the fix cell above in your current notebook

The fix ensures `onnx` is installed before conversion.
