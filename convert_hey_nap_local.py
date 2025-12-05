#!/usr/bin/env python3
"""
Local version of convert_hey_nap_COMPATIBLE.ipynb
Converts ONNX to TFLite locally.
"""

import os
import sys
import tempfile

def main():
    onnx_file = "hey_nap.onnx"
    tflite_file = "hey_nap.tflite"
    
    # Check if ONNX file exists
    if not os.path.exists(onnx_file):
        print(f"❌ File not found: {onnx_file}")
        print("\nAvailable .onnx files:")
        for f in os.listdir("."):
            if f.endswith(".onnx"):
                print(f"  - {f}")
        sys.exit(1)
    
    print(f"✅ Found ONNX file: {onnx_file}")
    print(f"   Size: {os.path.getsize(onnx_file) / 1024:.1f} KB\n")
    
    # Step 1: Try to import - if fails, suggest installation
    try:
        import onnx
        import tensorflow as tf
        from onnx_tf.backend import prepare
        print(f"✅ Dependencies found:")
        print(f"   ONNX: {onnx.__version__}")
        print(f"   TensorFlow: {tf.__version__}")
    except ImportError as e:
        print(f"❌ Missing dependency: {e}")
        print("\n💡 Install dependencies:")
        print("   pip install 'onnx==1.14.1' 'onnx-tf==1.10.0' tensorflow")
        print("\n⚠️  Note: Python 3.13 may not be compatible with onnx-tf")
        print("   Consider using Colab (Python 3.12) instead")
        sys.exit(1)
    
    # Step 2: Convert
    print(f"\n🔄 Converting {onnx_file} to TFLite...")
    
    try:
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
        
        file_size = os.path.getsize(tflite_file) / 1024
        print(f"\n✅ Conversion successful!")
        print(f"   File: {tflite_file}")
        print(f"   Size: {file_size:.1f} KB")
        
        print(f"\n🧪 Test the model:")
        print(f"   python3 test_hey_nap_local.py --model {tflite_file}")
        
        return True
        
    except Exception as e:
        print(f"\n❌ Conversion failed: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
