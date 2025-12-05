# Quick Fix for Colab

## Problem
Building `onnx==1.14.1` from source fails in Colab.

## Solution
**Use Colab's default ONNX** (already installed) and just install `onnx2tf`.

## Updated Cell 1

Replace the first cell with this:

```python
# Install dependencies - use Colab's default ONNX (no build needed)
!pip install -q tensorflow
!pip install -q onnx2tf
print("✅ Dependencies installed")
```

**That's it!** Colab already has ONNX installed, so we don't need to install a specific version. The `onnx2tf` tool will work with whatever ONNX version Colab has.

## Why This Works

- Colab comes with ONNX pre-installed
- `onnx2tf` is compatible with the default ONNX version
- No compilation needed = faster and more reliable

Just update Cell 1 and run it again!
