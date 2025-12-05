#!/usr/bin/env python3
"""
Local version of convert_hey_nap.ipynb
Tests the conversion using onnx2tf
"""

import os
import sys
import subprocess
import glob

def main():
    onnx_file = "hey_nap.onnx"
    tflite_file = "hey_nap.tflite"
    
    print("=" * 60)
    print("Local ONNX to TFLite Conversion Test")
    print("=" * 60 + "\n")
    
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
    
    # Step 1: Check/install dependencies
    print("📦 Checking dependencies...")
    
    try:
        import onnx
        print(f"   ✅ ONNX: {onnx.__version__}")
    except ImportError:
        print("   ⚠️  ONNX not found - installing...")
        subprocess.run([sys.executable, "-m", "pip", "install", "-q", "onnx==1.14.1"], check=False)
        import onnx
        print(f"   ✅ ONNX installed: {onnx.__version__}")
    
    # Check if onnx2tf is available
    try:
        result = subprocess.run(["onnx2tf", "--version"], capture_output=True, text=True, timeout=5)
        print("   ✅ onnx2tf found")
    except (FileNotFoundError, subprocess.TimeoutExpired):
        print("   ⚠️  onnx2tf not found - installing...")
        subprocess.run([sys.executable, "-m", "pip", "install", "-q", "onnx2tf"], check=False)
        print("   ✅ onnx2tf installed")
    
    # Step 2: Convert
    print(f"\n🔄 Converting {onnx_file} to TFLite...")
    print("   This may take a minute...\n")
    
    try:
        # Run onnx2tf
        cmd = ["onnx2tf", "-i", onnx_file, "-o", ".", "-osd"]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        
        if result.returncode != 0:
            print(f"❌ Conversion failed:")
            print(result.stderr)
            return False
        
        # Find generated TFLite file
        tflite_files = glob.glob("*.tflite")
        
        if tflite_files:
            # Rename to our desired name
            generated_file = tflite_files[0]
            if generated_file != tflite_file and os.path.exists(generated_file):
                if os.path.exists(tflite_file):
                    os.remove(tflite_file)
                os.rename(generated_file, tflite_file)
            
            file_size = os.path.getsize(tflite_file) / 1024
            print(f"✅ Conversion successful!")
            print(f"   File: {tflite_file}")
            print(f"   Size: {file_size:.1f} KB")
            
            print(f"\n🧪 Test the model:")
            print(f"   python3 test_hey_nap_local.py --model {tflite_file}")
            
            return True
        else:
            print("❌ TFLite file not found after conversion")
            print("\nAvailable files:")
            for f in os.listdir("."):
                if f.endswith((".tflite", ".onnx")):
                    print(f"  - {f}")
            return False
            
    except subprocess.TimeoutExpired:
        print("❌ Conversion timed out")
        return False
    except Exception as e:
        print(f"❌ Conversion failed: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
