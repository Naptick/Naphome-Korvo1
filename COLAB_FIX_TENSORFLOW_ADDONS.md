# Fix for tensorflow-addons Error

## Problem
`onnx-tf==1.10.0` tries to install `tensorflow-addons` which is deprecated and no longer available.

## Solution: Use Alternative Method

**Replace Cell 1 with this:**

```python
# Install dependencies - use onnx2tf instead (newer, better maintained)
!pip install -q "onnx==1.14.1" tensorflow
!pip install -q onnx2tf
print("✅ Dependencies installed")
```

**Then replace Cell 3 (conversion) with this:**

```python
# Convert ONNX to TFLite using onnx2tf
import os
import subprocess

onnx_file = "hey_nap.onnx"
tflite_file = "hey_nap.tflite"

print(f"🔄 Converting {onnx_file} to TFLite...")

# Use onnx2tf command-line tool
!onnx2tf -i {onnx_file} -o . -osd

# Find the generated TFLite file
import glob
tflite_files = glob.glob("*.tflite")
if tflite_files:
    # Rename to our desired name
    generated_file = tflite_files[0]
    if generated_file != tflite_file:
        os.rename(generated_file, tflite_file)
    print(f"\n✅ Conversion successful!")
    print(f"   File: {tflite_file}")
    print(f"   Size: {os.path.getsize(tflite_file) / 1024:.1f} KB")
else:
    print("❌ TFLite file not found")
```

## Alternative: Try Without tensorflow-addons

If you want to stick with onnx-tf, try this in Cell 1:

```python
# Install tensorflow first, then onnx-tf without tensorflow-addons
!pip install -q tensorflow
!pip install -q "onnx==1.14.1"
!pip install -q "onnx-tf==1.10.0" --no-deps
# Manually install onnx-tf dependencies (except tensorflow-addons)
!pip install -q protobuf numpy six typing-extensions
print("✅ Dependencies installed (without tensorflow-addons)")
```

The `onnx2tf` method is recommended as it's actively maintained.
