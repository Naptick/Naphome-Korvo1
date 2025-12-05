# Local Conversion Guide

## Problem
Python 3.13 is not compatible with `onnx-tf` (the ONNX to TensorFlow converter).

## Solutions

### Option 1: Use Colab (Easiest) ✅
- Colab uses Python 3.12 (compatible)
- No local setup needed
- Just upload the notebook: `convert_hey_nap.ipynb`

### Option 2: Use Python 3.11 or 3.12 Locally

#### Check if you have Python 3.11/3.12:
```bash
which python3.12
which python3.11
```

#### If you have it, use it:
```bash
python3.12 convert_hey_nap_local.py
```

#### If you don't have it, install with Homebrew:
```bash
brew install python@3.12
python3.12 convert_hey_nap_local.py
```

### Option 3: Use Docker

Create a Docker container with Python 3.12:

```bash
# Create Dockerfile
cat > Dockerfile << EOF
FROM python:3.12-slim

WORKDIR /app
COPY convert_hey_nap_local.py .
COPY hey_nap.onnx .

RUN pip install --prefer-binary onnx tensorflow onnx-tf --no-deps && \
    pip install protobuf numpy six typing-extensions

CMD ["python", "convert_hey_nap_local.py"]
EOF

# Build and run
docker build -t convert-onnx .
docker run -v $(pwd):/app/output convert-onnx
```

### Option 4: Use pyenv to Switch Python Versions

```bash
# Install pyenv if needed
brew install pyenv

# Install Python 3.12
pyenv install 3.12.0

# Use it for this project
pyenv local 3.12.0

# Run conversion
python convert_hey_nap_local.py
```

## Quick Test

After setting up Python 3.11 or 3.12:

```bash
python3.12 convert_hey_nap_local.py
```

The script will:
1. Check dependencies
2. Install missing ones
3. Convert `hey_nap.onnx` to `hey_nap.tflite`

## Recommendation

**Use Colab** - it's the simplest and most reliable option. The notebook `convert_hey_nap.ipynb` is ready to use.
