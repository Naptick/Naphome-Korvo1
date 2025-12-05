#!/usr/bin/env python3
"""
Simplest possible conversion - try TensorFlow's built-in ONNX support.
"""

import os
import sys
import tempfile

onnx_file = "hey_nap.onnx"
tflite_file = "hey_nap.tflite"

print("🔄 Trying TensorFlow's built-in ONNX support...")

try:
    import tensorflow as tf
    
    # Check if TF has ONNX support
    print(f"TensorFlow version: {tf.__version__}")
    
    # Try tf.compat.v1 or other methods
    # Note: TensorFlow doesn't have built-in ONNX import
    
    print("   ⚠️  TensorFlow doesn't have built-in ONNX import")
    print("   Need to use onnx-tf or onnx2tf")
    
except Exception as e:
    print(f"   ❌ Failed: {e}")
