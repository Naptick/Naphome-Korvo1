# Copy/Paste This EXACT Code

## ⚠️ Important: Delete your old cell and paste this NEW one

**This is the COMPLETE cell - it installs everything first:**

```python
# ============================================
# COMPLETE CONVERSION CELL - COPY ALL OF THIS
# ============================================

import os
import tempfile
from google.colab import files

# STEP 1: Install dependencies (MUST RUN FIRST)
print("📦 Installing dependencies...")
!pip install -q "onnx==1.14.1" "onnx-tf==1.10.0" tensorflow
print("✅ Installation complete\n")

# STEP 2: Upload file
print("📤 Upload your ONNX file...")
uploaded = files.upload()
onnx_file = list(uploaded.keys())[0]

# Rename to remove spaces
if onnx_file != "hey_nap.onnx":
    os.rename(onnx_file, "hey_nap.onnx")
    onnx_file = "hey_nap.onnx"
    print(f"✅ Renamed to: hey_nap.onnx")

print(f"✅ File ready: {onnx_file}\n")

# STEP 3: Import (AFTER installation)
import onnx
import tensorflow as tf
from onnx_tf.backend import prepare

tflite_file = "hey_nap.tflite"

# STEP 4: Convert
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

# STEP 5: Download
print(f"\n📥 Downloading {tflite_file}...")
files.download(tflite_file)
print(f"\n✅ Download complete!")
print(f"\n🎉 Your TFLite model is ready!")
```

## Instructions

1. **Delete your current cell** (the one with the error)
2. **Create a NEW cell**
3. **Paste the ENTIRE code above** (from `# ============================================` to the end)
4. **Run the cell**
5. **Upload your ONNX file** when prompted

## If It Still Fails

If you still get an error, try this:

1. **Runtime** → **Restart runtime**
2. **Run the cell again**

The key is: **The `!pip install` command MUST run BEFORE the `import onnx` line!**
