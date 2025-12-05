# Easy ONNX to TFLite Conversion

## ✅ Your Model is Ready!

You have: `hey_nap.onnx` (201KB) - Your trained model!

## 🚀 Easiest Way to Convert

### Option 1: Online Converter (Fastest - 2 minutes)

1. **Go to:** https://convertmodel.com/
2. **Click:** "Choose File"
3. **Select:** `hey_nap.onnx` (from your Downloads or project)
4. **Select format:** ONNX → TFLite
5. **Click:** Convert
6. **Download:** `hey_nap.tflite`

**Done!** Then test it:
```bash
python3 test_hey_nap_local.py --model hey_nap.tflite
```

### Option 2: Use Colab (If online converter doesn't work)

Create a **new Colab notebook** and run:

```python
from google.colab import files
import os

# Upload ONNX file
uploaded = files.upload()
onnx_file = list(uploaded.keys())[0]

# Install and convert
!pip install -q onnx2tf tensorflow
!onnx2tf -i {onnx_file} -o . -osd

# Download result
tflite_file = onnx_file.replace('.onnx', '_float32.tflite')
if os.path.exists(tflite_file):
    files.download(tflite_file)
```

## 🧪 Test Your Model

After conversion, test with different thresholds:

```bash
# Try lower threshold
python3 test_hey_nap_local.py --model hey_nap.tflite --threshold 0.1

# Or test with ONNX (works for testing)
python3 test_hey_nap_local.py --model hey_nap.onnx --threshold 0.1
```

## 📝 Current Status

- ✅ Model trained successfully
- ✅ ONNX file ready: `hey_nap.onnx`
- ⏳ Need to convert to TFLite for ESP32
- ✅ Test script ready

## 🎯 Next Steps

1. **Convert:** Use https://convertmodel.com/
2. **Test:** `python3 test_hey_nap_local.py --model hey_nap.tflite --threshold 0.1`
3. **Deploy:** Copy to `components/openwakeword/models/`

The model is trained and ready - just needs TFLite conversion!
