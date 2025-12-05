#!/usr/bin/env python3
"""
Alternative conversion using ONNX Runtime as intermediate step.
This might work better than direct onnx-tf conversion.
"""

import os
import sys
import tempfile
import numpy as np

def convert_via_onnxruntime():
    """Try converting via ONNX Runtime."""
    onnx_file = "hey_nap.onnx"
    tflite_file = "hey_nap.tflite"
    
    print("🔄 Trying ONNX Runtime approach...")
    
    try:
        import onnxruntime as ort
        import tensorflow as tf
        
        print("Step 1: Loading ONNX model with ONNX Runtime...")
        session = ort.InferenceSession(onnx_file)
        print("   ✅ ONNX Runtime session created")
        
        # Get input/output info
        input_name = session.get_inputs()[0].name
        input_shape = session.get_inputs()[0].shape
        print(f"   Input: {input_name}, Shape: {input_shape}")
        
        # This approach is complex - ONNX Runtime doesn't directly convert to TF
        # We'd need to trace the model execution
        print("   ⚠️  ONNX Runtime doesn't directly convert to TensorFlow")
        print("   This approach needs model tracing/export")
        
        return False
        
    except ImportError:
        print("   ⚠️  onnxruntime not installed")
        print("   Install: pip install onnxruntime")
        return False
    except Exception as e:
        print(f"   ❌ Failed: {e}")
        return False

if __name__ == "__main__":
    convert_via_onnxruntime()
