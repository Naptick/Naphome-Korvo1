# Final Fix - Install ONNX from Wheel

## Problem
Colab doesn't have ONNX installed, and building from source fails.

## Solution: Use `--prefer-binary` Flag

**Updated Cell 1 - Copy/Paste This:**

```python
# Install dependencies - install ONNX from wheel (no build)
!pip install -q tensorflow
# Install ONNX from pre-built wheel (use version with wheels available)
!pip install -q onnx --prefer-binary
# Install onnx-tf
!pip install -q onnx-tf --no-deps
!pip install -q protobuf numpy six typing-extensions
print("✅ Dependencies installed")
```

## What `--prefer-binary` Does

- Tells pip to prefer pre-built wheels over source distributions
- If a wheel is available, it uses it (no compilation)
- If no wheel is available, it falls back to source (but should find one)

## Alternative: If Still Fails

If `--prefer-binary` still tries to build, try this:

```python
# Force use of wheel only - fail if wheel not available
!pip install -q tensorflow
!pip install -q --only-binary :all: onnx
!pip install -q onnx-tf --no-deps
!pip install -q protobuf numpy six typing-extensions
print("✅ Dependencies installed")
```

The `--only-binary :all:` flag will only use wheels and never build from source.

## Test After Installation

```python
import onnx
print(f"ONNX version: {onnx.__version__}")
```

If this works, you're ready to convert!
