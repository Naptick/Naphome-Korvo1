# Fix for Filenames with Spaces

## The Problem
Your file is named `hey_nap (1).onnx` which has spaces and parentheses. The shell command fails because of this.

## Quick Fix - Run This in Colab

**Replace your conversion cell with this:**

```python
from pathlib import Path
import os
import shlex

model_name = Path(onnx_file).stem
tflite_file = f"{model_name}.tflite"

print(f"🔄 Converting {onnx_file} to TFLite...")
print("\\nInstalling dependencies...")
!pip install -q onnx onnx2tf

print("\\nConverting (this may take a minute)...")
# Quote the filename to handle spaces
onnx_file_quoted = shlex.quote(onnx_file)
!onnx2tf -i {onnx_file_quoted} -o . -osd

# Check for output
output_file = f"{model_name}_float32.tflite"
if os.path.exists(output_file):
    os.rename(output_file, tflite_file)
    print(f"\\n✅ Conversion successful!")
    print(f"   File: {tflite_file}")
else:
    print("\\n⚠️  Output not found, checking...")
    !ls -la *.tflite 2>/dev/null || echo "No TFLite files"
```

**Or rename the file first:**

```python
# Rename file to remove spaces
import os
new_name = "hey_nap.onnx"
if onnx_file != new_name:
    os.rename(onnx_file, new_name)
    onnx_file = new_name
    print(f"Renamed to: {new_name}")

# Then convert normally
!onnx2tf -i {onnx_file} -o . -osd
```

The key is using `shlex.quote()` to properly escape the filename, or renaming it first.
