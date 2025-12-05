#!/usr/bin/env python3
"""
Simple ONNX to TFLite converter using onnxruntime and tensorflow.
"""

import sys
import os
import numpy as np

def convert_onnx_to_tflite_simple(onnx_path, tflite_path):
    """Convert ONNX to TFLite using a simpler approach."""
    
    print(f"Converting {onnx_path} to TFLite...")
    
    try:
        # Method 1: Try using onnxruntime to get model info, then convert
        import onnxruntime as ort
        
        # Load ONNX model to get input/output info
        session = ort.InferenceSession(onnx_path)
        input_name = session.get_inputs()[0].name
        input_shape = session.get_inputs()[0].shape
        
        print(f"Model input: {input_name}, shape: {input_shape}")
        
        # Create a dummy input
        if None in input_shape:
            # Replace None with 1 for batch dimension
            input_shape = [1 if x is None else x for x in input_shape]
        
        dummy_input = np.random.randn(*input_shape).astype(np.float32)
        
        # Try TensorFlow conversion
        try:
            import tensorflow as tf
            from onnx_tf.backend import prepare
            import onnx
            
            print("Loading ONNX model...")
            onnx_model = onnx.load(onnx_path)
            
            print("Converting to TensorFlow...")
            tf_rep = prepare(onnx_model)
            
            # Export to SavedModel
            import tempfile
            with tempfile.TemporaryDirectory() as tmp_dir:
                tf_model_path = os.path.join(tmp_dir, "saved_model")
                tf_rep.export_graph(tf_model_path)
                
                print("Converting to TFLite...")
                converter = tf.lite.TFLiteConverter.from_saved_model(tf_model_path)
                tflite_model = converter.convert()
                
                with open(tflite_path, 'wb') as f:
                    f.write(tflite_model)
                
                print(f"✅ Success! TFLite model saved: {tflite_path}")
                return True
                
        except ImportError as e:
            print(f"TensorFlow conversion failed: {e}")
            print("\nTrying alternative method...")
            
    except Exception as e:
        print(f"Error: {e}")
        return False
    
    # Alternative: Use command line onnx2tf if available
    try:
        import subprocess
        output_dir = os.path.dirname(tflite_path) or "."
        os.makedirs(output_dir, exist_ok=True)
        
        print("Trying onnx2tf command line tool...")
        result = subprocess.run(
            ["onnx2tf", "-i", onnx_path, "-o", output_dir, "-osd"],
            capture_output=True,
            text=True,
            timeout=300
        )
        
        if result.returncode == 0:
            base_name = os.path.splitext(os.path.basename(onnx_path))[0]
            output_file = os.path.join(output_dir, f"{base_name}_float32.tflite")
            
            if os.path.exists(output_file):
                if output_file != tflite_path:
                    import shutil
                    shutil.move(output_file, tflite_path)
                print(f"✅ Success! TFLite model saved: {tflite_path}")
                return True
        else:
            print(f"onnx2tf failed: {result.stderr}")
            
    except Exception as e:
        print(f"Command line conversion failed: {e}")
    
    return False

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 convert_simple.py input.onnx output.tflite")
        sys.exit(1)
    
    onnx_path = sys.argv[1]
    tflite_path = sys.argv[2]
    
    if not os.path.exists(onnx_path):
        print(f"Error: File not found: {onnx_path}")
        sys.exit(1)
    
    success = convert_onnx_to_tflite_simple(onnx_path, tflite_path)
    
    if not success:
        print("\n❌ Conversion failed")
        print("\n💡 Alternative options:")
        print("   1. Use a different Python environment")
        print("   2. Try the Colab conversion method")
        print("   3. Use the ONNX model directly (works for testing)")
        sys.exit(1)
