#!/usr/bin/env python3
"""
Local ONNX to TFLite conversion script.
Converts hey_nap.onnx to hey_nap.tflite for ESP32 deployment.
"""

import os
import sys
import subprocess
import tempfile

def check_dependencies():
    """Check and install dependencies if needed."""
    print("📦 Checking dependencies...\n")
    
    deps_ok = True
    
    # Check ONNX
    try:
        import onnx
        print(f"✅ ONNX: {onnx.__version__}")
    except ImportError:
        print("⚠️  ONNX not found - installing...")
        try:
            subprocess.run([sys.executable, "-m", "pip", "install", "--prefer-binary", "onnx"], 
                         check=True, capture_output=True)
            import onnx
            print(f"✅ ONNX installed: {onnx.__version__}")
        except Exception as e:
            print(f"❌ Failed to install ONNX: {e}")
            deps_ok = False
    
    # Check TensorFlow
    try:
        import tensorflow as tf
        print(f"✅ TensorFlow: {tf.__version__}")
    except ImportError:
        print("⚠️  TensorFlow not found - installing...")
        try:
            subprocess.run([sys.executable, "-m", "pip", "install", "tensorflow"], 
                         check=True, capture_output=True)
            import tensorflow as tf
            print(f"✅ TensorFlow installed: {tf.__version__}")
        except Exception as e:
            print(f"❌ Failed to install TensorFlow: {e}")
            deps_ok = False
    
    # Check onnx-tf
    try:
        from onnx_tf.backend import prepare
        print("✅ onnx-tf: available")
    except ImportError:
        print("⚠️  onnx-tf not found - installing...")
        try:
            subprocess.run([sys.executable, "-m", "pip", "install", "onnx-tf", "--no-deps"], 
                         check=True, capture_output=True)
            subprocess.run([sys.executable, "-m", "pip", "install", "protobuf", "numpy", "six", "typing-extensions"], 
                         check=True, capture_output=True)
            from onnx_tf.backend import prepare
            print("✅ onnx-tf installed")
        except Exception as e:
            print(f"❌ Failed to install onnx-tf: {e}")
            print("\n💡 Note: Python 3.13 may not be compatible with onnx-tf")
            print("   Consider using Python 3.11 or 3.12, or use Colab")
            deps_ok = False
    
    print()
    return deps_ok

def convert_onnx_to_tflite(onnx_file, tflite_file):
    """Convert ONNX to TFLite."""
    print(f"🔄 Converting {onnx_file} to TFLite...\n")
    
    try:
        import onnx
        import tensorflow as tf
        from onnx_tf.backend import prepare
        
        print("Step 1: Loading ONNX model...")
        onnx_model = onnx.load(onnx_file)
        print(f"   ✅ ONNX model loaded (ONNX version: {onnx.__version__})")
        
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
        
        return True
        
    except Exception as e:
        print(f"\n❌ Conversion failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    onnx_file = "hey_nap.onnx"
    tflite_file = "hey_nap.tflite"
    
    print("=" * 60)
    print("Local ONNX to TFLite Conversion")
    print("=" * 60 + "\n")
    
    # Check Python version
    python_version = sys.version_info
    print(f"Python version: {python_version.major}.{python_version.minor}.{python_version.micro}")
    if python_version.major == 3 and python_version.minor >= 13:
        print("⚠️  Warning: Python 3.13+ may have compatibility issues with onnx-tf")
        print("   Consider using Python 3.11 or 3.12, or use Colab\n")
    
    # Check if ONNX file exists
    if not os.path.exists(onnx_file):
        print(f"❌ ONNX file not found: {onnx_file}")
        print("\nAvailable .onnx files:")
        for f in os.listdir("."):
            if f.endswith(".onnx"):
                print(f"  - {f}")
        sys.exit(1)
    
    print(f"✅ Found ONNX file: {onnx_file}")
    print(f"   Size: {os.path.getsize(onnx_file) / 1024:.1f} KB\n")
    
    # Check/install dependencies
    if not check_dependencies():
        print("❌ Dependency check failed")
        print("\n💡 Alternatives:")
        print("   1. Use Colab (recommended)")
        print("   2. Use Python 3.11 or 3.12")
        print("   3. Use Docker with compatible Python version")
        sys.exit(1)
    
    # Convert
    success = convert_onnx_to_tflite(onnx_file, tflite_file)
    
    if success:
        print(f"\n🧪 Test the model:")
        print(f"   python3 test_hey_nap_local.py --model {tflite_file}")
        sys.exit(0)
    else:
        print("\n💡 If conversion failed due to compatibility issues:")
        print("   - Try using Colab (Python 3.12)")
        print("   - Or use Python 3.11/3.12 locally")
        sys.exit(1)

if __name__ == "__main__":
    main()
