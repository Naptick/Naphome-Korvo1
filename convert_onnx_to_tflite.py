#!/usr/bin/env python3
"""
Convert ONNX model to TFLite format.

Usage:
    python3 convert_onnx_to_tflite.py input.onnx output.tflite
"""

import sys
import os
import tempfile
import shutil

def convert_onnx_to_tflite(onnx_path, tflite_path):
    """Convert ONNX model to TFLite format."""
    
    if not os.path.exists(onnx_path):
        print(f"❌ ONNX file not found: {onnx_path}")
        return False
    
    print(f"📦 Converting {onnx_path} to TFLite...")
    
    try:
        # Try method 1: onnx-tf
        try:
            import onnx
            from onnx_tf.backend import prepare
            import tensorflow as tf
            
            print("   Using onnx-tf...")
            onnx_model = onnx.load(onnx_path)
            tf_rep = prepare(onnx_model)
            
            with tempfile.TemporaryDirectory() as tmp_dir:
                tf_model_path = os.path.join(tmp_dir, "tf_model")
                tf_rep.export_graph(tf_model_path)
                
                converter = tf.lite.TFLiteConverter.from_saved_model(tf_model_path)
                tflite_model = converter.convert()
                
                with open(tflite_path, 'wb') as f:
                    f.write(tflite_model)
                
                print(f"✅ TFLite model saved: {tflite_path}")
                return True
                
        except ImportError:
            print("   onnx-tf not available, trying alternative...")
            raise
        
    except Exception as e1:
        print(f"   Method 1 failed: {e1}")
        
        # Try method 2: onnx2tf command line
        try:
            print("   Trying onnx2tf command line tool...")
            import subprocess
            
            output_dir = os.path.dirname(tflite_path) or "."
            os.makedirs(output_dir, exist_ok=True)
            
            result = subprocess.run(
                ["onnx2tf", "-i", onnx_path, "-o", output_dir, "-osd"],
                capture_output=True,
                text=True
            )
            
            if result.returncode == 0:
                # Find the output file
                base_name = os.path.splitext(os.path.basename(onnx_path))[0]
                output_file = os.path.join(output_dir, f"{base_name}_float32.tflite")
                
                if os.path.exists(output_file):
                    if output_file != tflite_path:
                        shutil.move(output_file, tflite_path)
                    print(f"✅ TFLite model saved: {tflite_path}")
                    return True
                else:
                    print(f"⚠️  Output file not found: {output_file}")
                    print(f"   Available files in {output_dir}:")
                    for f in os.listdir(output_dir):
                        print(f"     - {f}")
            else:
                print(f"   onnx2tf failed: {result.stderr}")
                raise
                
        except Exception as e2:
            print(f"   Method 2 failed: {e2}")
            print("\n❌ All conversion methods failed")
            print("\n💡 Install dependencies:")
            print("   pip install onnx-tf tensorflow")
            print("   # or")
            print("   pip install onnx2tf")
            return False
    
    return False

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 convert_onnx_to_tflite.py input.onnx output.tflite")
        sys.exit(1)
    
    onnx_path = sys.argv[1]
    tflite_path = sys.argv[2]
    
    success = convert_onnx_to_tflite(onnx_path, tflite_path)
    
    if success:
        print(f"\n✅ Conversion complete!")
        print(f"   TFLite model: {os.path.abspath(tflite_path)}")
        print(f"\n🧪 Test it:")
        print(f"   python3 test_hey_nap_local.py --model {tflite_path}")
    else:
        print(f"\n❌ Conversion failed")
        print(f"   You can still use the ONNX model, but ESP32 needs TFLite")
        sys.exit(1)

if __name__ == "__main__":
    main()
