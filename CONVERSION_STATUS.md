# Conversion Status

## ❌ Current Status: Not Converted Yet

All conversion methods have encountered dependency issues:
- **Colab**: ONNX build failures, dependency conflicts
- **Local Python 3.13**: Not compatible with onnx-tf
- **Local Python 3.12**: ONNX version too new (missing `mapping` module)
- **Docker**: `tensorflow-addons` deprecated, `tf_keras` missing

## ✅ Recommended Solution: Online Converter

**This is the fastest and most reliable option right now:**

1. Go to: **https://convertmodel.com/**
2. Upload: `hey_nap.onnx` (201 KB)
3. Select: **ONNX → TFLite**
4. Download: `hey_nap.tflite`

**Alternative online converters:**
- https://netron.app/ (viewer, may have export)
- Search: "onnx to tflite online converter"

## 📝 After Conversion

Once you have `hey_nap.tflite`:

1. **Test it:**
   ```bash
   python3 test_hey_nap_local.py --model hey_nap.tflite
   ```

2. **Deploy to ESP32:**
   ```bash
   cp hey_nap.tflite components/openwakeword/models/
   ```

## 🔄 If You Want to Try Docker Again Later

The Docker setup is in place but needs dependency fixes. Files:
- `Dockerfile.convert`
- `convert_hey_nap_docker.py`
- `convert_with_docker.sh`

The issue is that `onnx2tf` requires `tf_keras` which isn't available in the TensorFlow versions we've tried.

## 💡 Future Fix

The conversion tools need updates to work with:
- Newer ONNX versions (1.20+)
- Newer TensorFlow versions (2.20+)
- Python 3.12/3.13

Until then, **online converters are the most reliable option**.
