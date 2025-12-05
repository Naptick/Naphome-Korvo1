# Fix for ONNX Build Error in Colab

## Problem
`onnx2tf` tries to install ONNX as a dependency, and it attempts to build from source which fails.

## Solution: Install Pre-built ONNX First

**Updated Cell 1 - Copy/Paste This:**

```python
# Install dependencies - use pre-built ONNX wheel first
!pip install -q tensorflow
# Install ONNX from pre-built wheel (avoids compilation)
!pip install -q --only-binary=all onnx
# Now install onnx2tf (it will use the ONNX we just installed)
!pip install -q onnx2tf
print("✅ Dependencies installed")
```

## What This Does

1. **`--only-binary=all onnx`**: Forces pip to only use pre-built wheels, never build from source
2. Installs ONNX first (from wheel)
3. Then installs `onnx2tf` (which will see ONNX is already installed)

## Alternative: If Still Fails

If you still get errors, try this simpler approach using `onnx-tf` but installing tensorflow first:

```python
# Alternative method - install tensorflow first, then onnx-tf
!pip install -q tensorflow
!pip install -q "onnx==1.15.0" --only-binary=all
!pip install -q "onnx-tf==1.10.0" --no-deps
!pip install -q protobuf numpy six typing-extensions
print("✅ Dependencies installed")
```

Then use the `onnx-tf` conversion method in Cell 3 instead of `onnx2tf`.

## Quick Test

After installing, verify:
```python
import onnx
print(f"ONNX version: {onnx.__version__}")
```

If this works, you're good to go!
