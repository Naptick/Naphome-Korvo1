# Working Installation Cell for Colab

## Copy/Paste This Complete Cell 1

```python
# Install dependencies - simplest approach
!pip install -q tensorflow
!pip install -q onnx-tf --no-deps
!pip install -q protobuf numpy six typing-extensions
print("✅ Dependencies installed")
```

## Why This Works

- **No ONNX installation** - Uses Colab's existing ONNX
- **`--no-deps`** - Prevents pip from trying to install ONNX as a dependency
- **Only installs what's needed** - Just the onnx-tf package and its minimal dependencies

## Then Use This Cell 3 (Conversion)

```python
# Convert ONNX to TFLite using onnx-tf
import onnx
import tensorflow as tf
from onnx_tf.backend import prepare
import tempfile
import os

onnx_file = "hey_nap.onnx"
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

This avoids all the ONNX build issues!
