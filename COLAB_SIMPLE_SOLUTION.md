# Simple Solution - Skip ONNX Installation

## Problem
ONNX keeps trying to build from source and failing.

## Solution: Use Colab's Existing ONNX

**Updated Cell 1 - Just install tensorflow and onnx2tf:**

```python
# Install dependencies - use Colab's existing ONNX
!pip install -q tensorflow
!pip install -q onnx2tf --no-build-isolation
print("✅ Dependencies installed")
```

The `--no-build-isolation` flag tells pip to use the existing environment's packages instead of building new ones.

## If That Still Fails

**Use this alternative Cell 1 that installs onnx-tf instead:**

```python
# Alternative: Use onnx-tf (installs without building ONNX)
!pip install -q tensorflow
!pip install -q onnx-tf --no-deps
!pip install -q protobuf numpy six typing-extensions
print("✅ Dependencies installed")
```

Then use the **onnx-tf method** in Cell 3 (the notebook now has both methods with automatic fallback).

## Simplest Solution

If you want the absolute simplest approach, **just use this single cell** for installation:

```python
!pip install -q tensorflow onnx-tf
print("✅ Dependencies installed")
```

Then use the onnx-tf conversion method (which the updated notebook now includes as a fallback).

The updated notebook (`convert_hey_nap.ipynb`) now tries `onnx2tf` first, and if that fails, automatically falls back to the `onnx-tf` method which should work with Colab's existing ONNX.
