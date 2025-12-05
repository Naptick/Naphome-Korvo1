# Convert ONNX to TFLite - Alternative Methods

Your model is trained and works! The ONNX file is ready. Here are ways to convert it to TFLite:

## Method 1: Use Online Converter (Easiest)

1. **Go to:** https://convertmodel.com/
2. **Upload:** `hey_nap.onnx`
3. **Select:** ONNX → TFLite
4. **Download:** `hey_nap.tflite`

## Method 2: Use Colab (Recommended)

Run this in a **new Colab notebook**:

```python
from google.colab import files
import os

# Upload your ONNX file
uploaded = files.upload()
onnx_file = list(uploaded.keys())[0]

# Install converter
!pip install -q onnx2tf tensorflow

# Convert
!onnx2tf -i {onnx_file} -o . -osd

# Download TFLite
tflite_file = onnx_file.replace('.onnx', '_float32.tflite')
if os.path.exists(tflite_file):
    files.download(tflite_file)
```

## Method 3: Use Docker (If you have Docker)

```bash
docker run -it --rm -v $(pwd):/workspace onnx/onnx-tf:latest \
  onnx-tf convert -i /workspace/hey_nap.onnx -o /workspace/hey_nap.tflite
```

## Method 4: Test ONNX Model First

The ONNX model works! You can test it:

```bash
python3 test_hey_nap_local.py --model hey_nap.onnx
```

For ESP32, you'll need TFLite, but ONNX works for local testing.

## Quick Solution

**Easiest:** Use the online converter at https://convertmodel.com/

1. Upload `hey_nap.onnx`
2. Convert to TFLite
3. Download `hey_nap.tflite`
4. Test: `python3 test_hey_nap_local.py`
