---
title: Wake Word Training Results
layout: default
---

# Naphome Korvo1 - Wake Word Training Results

Welcome to the documentation for the "Hey Nap" wake word model training and deployment.

## Quick Links

- [Training & Deployment Results](./wake-word-training-results.md) - Complete results and recommendations
- [GitHub Repository](https://github.com/danielmcshan/Naphome-Korvo1) - Source code and project files

## Summary

Successfully trained and deployed a custom "Hey Nap" wake word model for ESP32-S3 Korvo1:

- ✅ **Model Converted:** ONNX → TFLite (202 KB)
- ✅ **Model Deployed:** Ready for ESP32 integration
- ✅ **Conversion Pipeline:** Fully functional Docker-based solution
- ⚠️  **Performance:** Low scores (needs training improvements)

## Key Results

**Model Status:**
- Format: TFLite (ESP32-compatible)
- Size: 202 KB
- Location: `components/openwakeword/models/hey_nap.tflite`
- Test Results: Functional but low scores (max: 0.018)

**Conversion Pipeline:**
- Method: Docker-based (Python 3.11)
- Dependencies: TensorFlow 2.15.0, ONNX 1.14.1, onnx-tf 1.10.0
- Status: ✅ Working and tested

## Recommendations

1. **Collect More Training Data** (50+ diverse samples)
2. **Retrain with Better Configuration** (more epochs, better augmentation)
3. **Test on ESP32 Hardware** (real-world validation)
4. **Iterate Based on Results** (continuous improvement)

## Documentation

For complete details, see [Training & Deployment Results](./wake-word-training-results.md).

---

**Last Updated:** December 5, 2024
