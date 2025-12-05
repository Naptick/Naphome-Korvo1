#!/usr/bin/env python3
"""Docker version - converts ONNX to TFLite using onnx2tf with workaround."""

import os
import subprocess
import glob
import sys

# Try to make tf_keras available
try:
    import tensorflow as tf
    # Try to import tf_keras through tensorflow
    if hasattr(tf, 'keras'):
        # Create tf_keras alias
        import sys
        import types
        tf_keras_module = types.ModuleType('tf_keras')
        tf_keras_module.__dict__.update(tf.keras.__dict__)
        sys.modules['tf_keras'] = tf_keras_module
        print("✅ Created tf_keras workaround")
except Exception as e:
    print(f"⚠️  Workaround failed: {e}")

onnx_file = "hey_nap.onnx"
tflite_file = "hey_nap.tflite"

print(f"🔄 Converting {onnx_file} to TFLite using onnx2tf...")
print("   This may take a minute...\n")

# Use onnx2tf command-line tool
result = subprocess.run(
    ["python", "-m", "onnx2tf", "-i", onnx_file, "-o", ".", "-osd"],
    capture_output=True,
    text=True
)

if result.returncode != 0:
    print(f"❌ Conversion failed:")
    if result.stderr:
        print(result.stderr)
    if result.stdout:
        print(result.stdout)
    sys.exit(1)

# Find generated TFLite file
tflite_files = glob.glob("*.tflite")

if tflite_files:
    # Rename to our desired name
    generated_file = tflite_files[0]
    if generated_file != tflite_file:
        if os.path.exists(tflite_file):
            os.remove(tflite_file)
        os.rename(generated_file, tflite_file)
    
    file_size = os.path.getsize(tflite_file) / 1024
    print(f"✅ Conversion successful!")
    print(f"   File: {tflite_file}")
    print(f"   Size: {file_size:.1f} KB")
else:
    print("❌ TFLite file not found after conversion")
    print("\nAvailable files:")
    for f in os.listdir("."):
        if f.endswith((".tflite", ".onnx")):
            print(f"  - {f}")
    sys.exit(1)
