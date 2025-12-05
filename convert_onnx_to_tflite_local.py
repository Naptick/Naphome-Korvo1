#!/usr/bin/env python3
"""
Convert ONNX to TFLite locally using compatible versions.
"""

import sys
import os
import tempfile

def convert_onnx_to_tflite(onnx_path, tflite_path):
    """Convert ONNX to TFLite using onnx-tf."""
    
    print(f"🔄 Converting {onnx_path} to TFLite...")
    
    try:
        import onnx
        import tensorflow as tf
        from onnx_tf.backend import prepare
        
        print("\nStep 1: Loading ONNX model...")
        onnx_model = onnx.load(onnx_path)
        print(f"   ✅ ONNX model loaded (version: {onnx.__version__})")
        
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
            with open(tflite_path, 'wb') as f:
                f.write(tflite_model)
        
        file_size = os.path.getsize(tflite_path) / 1024
        print(f"\n✅ Conversion successful!")
        print(f"   File: {tflite_path}")
        print(f"   Size: {file_size:.1f} KB")
        return True
        
    except ImportError as e:
        print(f"\n❌ Import error: {e}")
        print("\n💡 Install compatible versions:")
        print("   pip install 'onnx==1.15.0' 'onnx-tf==1.11.0' tensorflow")
        return False
    except Exception as e:
        print(f"\n❌ Conversion failed: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 convert_onnx_to_tflite_local.py input.onnx output.tflite")
        sys.exit(1)
    
    onnx_path = sys.argv[1]
    tflite_path = sys.argv[2]
    
    if not os.path.exists(onnx_path):
        print(f"❌ File not found: {onnx_path}")
        sys.exit(1)
    
    success = convert_onnx_to_tflite(onnx_path, tflite_path)
    
    if success:
        print(f"\n🧪 Test the model:")
        print(f"   python3 test_hey_nap_local.py --model {tflite_path}")
        sys.exit(0)
    else:
        sys.exit(1)
