#!/bin/bash
# Convert ONNX to TFLite using Docker

set -e

echo "🐳 Building Docker image..."
docker build -f Dockerfile.convert -t convert-onnx .

echo ""
echo "🔄 Converting hey_nap.onnx to hey_nap.tflite..."
docker run --rm \
    -v "$(pwd):/app" \
    -w /app \
    convert-onnx

echo ""
echo "✅ Conversion complete!"
echo "   File: hey_nap.tflite"
ls -lh hey_nap.tflite
