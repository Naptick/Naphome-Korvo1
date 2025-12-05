# Final Colab Conversion Instructions

## ✅ Working Solution for Colab

I've created `convert_hey_nap_COLAB_FINAL.ipynb` which uses compatible versions that work in Colab (Python 3.12).

## 🚀 How to Use

### Option 1: Upload the Fixed Notebook

1. Go to: https://colab.research.google.com/
2. **File** → **Upload notebook**
3. Upload: `convert_hey_nap_COLAB_FINAL.ipynb`
4. Run all cells

### Option 2: Use This Code in Your Current Notebook

**Cell 1: Install Compatible Versions**
```python
!pip install -q "onnx==1.14.1" "onnx-tf==1.10.0" tensorflow
print("✅ Dependencies installed")
```

**Cell 2: Upload and Rename**
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
```

**Cell 3: Convert**
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

**Cell 4: Download**
```python
from google.colab import files
import os

tflite_file = "hey_nap.tflite"

if os.path.exists(tflite_file):
    print(f"📥 Downloading {tflite_file}...")
    files.download(tflite_file)
    print(f"\n✅ Download complete!")
else:
    print(f"❌ File not found")
```

## 🔑 Key Points

1. **Use compatible versions:** `onnx==1.14.1` and `onnx-tf==1.10.0`
2. **Rename file first:** Removes spaces/special chars
3. **Colab uses Python 3.12:** Which is compatible with these versions

## 📝 Why Local Conversion Failed

- Your local Python is 3.13 (too new)
- `onnx-tf` doesn't support Python 3.13 yet
- Colab uses Python 3.12, which works

## ✅ Solution

**Use Colab for conversion** - it has the right Python version and dependencies.

The notebook `convert_hey_nap_COLAB_FINAL.ipynb` is ready to use!
