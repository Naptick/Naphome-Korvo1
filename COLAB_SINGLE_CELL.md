# Single Cell Solution for Colab

## Copy/Paste This Complete Cell

**This single cell does everything - installs dependencies, converts, and downloads:**

```python
# Complete conversion in one cell
import os
import tempfile
from google.colab import files

# Step 1: Install dependencies
print("📦 Installing dependencies...")
!pip install -q "onnx==1.14.1" "onnx-tf==1.10.0" tensorflow
print("✅ Dependencies installed\n")

# Step 2: Upload file (if not already uploaded)
if not os.path.exists("hey_nap.onnx"):
    print("📤 Uploading ONNX file...")
    uploaded = files.upload()
    onnx_file = list(uploaded.keys())[0]
    
    # Rename to remove spaces
    if onnx_file != "hey_nap.onnx":
        os.rename(onnx_file, "hey_nap.onnx")
        print(f"✅ Renamed to: hey_nap.onnx")
else:
    print("✅ File already exists: hey_nap.onnx")

onnx_file = "hey_nap.onnx"
tflite_file = "hey_nap.tflite"

# Step 3: Convert
print(f"\n🔄 Converting {onnx_file} to TFLite...")

import onnx
import tensorflow as tf
from onnx_tf.backend import prepare

print("Step 1: Loading ONNX model...")
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

# Step 4: Download
print(f"\n📥 Downloading {tflite_file}...")
files.download(tflite_file)
print(f"\n✅ Download complete!")
print(f"\n🎉 Your TFLite model is ready for ESP32!")
```

## How to Use

1. **Create a new cell** in your Colab notebook
2. **Paste the code above**
3. **Run the cell**
4. **Upload your ONNX file** when prompted
5. **Wait for conversion** (1-2 minutes)
6. **Download the TFLite file** automatically

This single cell handles everything!
