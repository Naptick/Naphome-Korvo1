# Working Colab Solution - Copy/Paste This

## The Problem
Version incompatibility between `onnx` and `onnx-tf`. Need compatible versions.

## Complete Working Solution

**Run these cells in order in Colab:**

### Cell 1: Install Compatible Versions
```python
# Install compatible versions
!pip install -q "onnx==1.15.0" "onnx-tf==1.11.0" tensorflow
print("✅ Dependencies installed")
```

### Cell 2: Upload and Rename File
```python
from google.colab import files
import os

print("📤 Upload your ONNX file...")
uploaded = files.upload()
onnx_file = list(uploaded.keys())[0]

# Rename to remove spaces
clean_name = "hey_nap.onnx"
if onnx_file != clean_name:
    os.rename(onnx_file, clean_name)
    onnx_file = clean_name
    print(f"✅ Renamed to: {clean_name}")

print(f"✅ File ready: {onnx_file}")
print(f"   Size: {os.path.getsize(onnx_file) / 1024:.1f} KB")
```

### Cell 3: Convert to TFLite
```python
import onnx
import tensorflow as tf
from onnx_tf.backend import prepare
import tempfile
import os

tflite_file = "hey_nap.tflite"

print(f"🔄 Converting {onnx_file} to TFLite...")
print("\nStep 1: Loading ONNX model...")
onnx_model = onnx.load(onnx_file)
print("   ✅ ONNX model loaded")

print("\nStep 2: Converting to TensorFlow...")
tf_rep = prepare(onnx_model)
print("   ✅ TensorFlow representation created")

print("\nStep 3: Exporting to SavedModel...")
with tempfile.TemporaryDirectory() as tmp_dir:
    tf_model_path = os.path.join(tmp_dir, "saved_model")
    tf_rep.export_graph(tf_model_path)
    print("   ✅ SavedModel exported")
    
    print("\nStep 4: Converting to TFLite...")
    converter = tf.lite.TFLiteConverter.from_saved_model(tf_model_path)
    tflite_model = converter.convert()
    
    print("\nStep 5: Saving TFLite file...")
    with open(tflite_file, 'wb') as f:
        f.write(tflite_model)

print(f"\n✅ Conversion successful!")
print(f"   File: {tflite_file}")
print(f"   Size: {os.path.getsize(tflite_file) / 1024:.1f} KB")
```

### Cell 4: Download
```python
from google.colab import files
import os

tflite_file = "hey_nap.tflite"

if os.path.exists(tflite_file):
    print(f"📥 Downloading {tflite_file}...")
    files.download(tflite_file)
    print(f"\n✅ Download complete!")
    print(f"\n🎉 Your TFLite model is ready!")
else:
    print(f"❌ File not found: {tflite_file}")
```

## Key Fix
The issue is version compatibility. Use:
- `onnx==1.15.0` (older version with `mapping` module)
- `onnx-tf==1.11.0` (compatible version)

This should work!
