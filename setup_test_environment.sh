#!/bin/bash
# Setup script for testing Hey Nap wake word locally on macOS

set -e

echo "=========================================="
echo "Setting up test environment for Hey Nap"
echo "=========================================="
echo ""

# Check if we're on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    echo "⚠️  Warning: This script is designed for macOS"
    echo "   The 'say' command may not be available on other systems"
fi

# Check Python
if ! command -v python3 &> /dev/null; then
    echo "❌ Python 3 not found. Please install Python 3."
    exit 1
fi

echo "✅ Python 3 found: $(python3 --version)"
echo ""

# Install Python dependencies
echo "📦 Installing Python dependencies..."
echo ""

# Try to install tflite-runtime (ESP32 compatible)
echo "Installing tflite-runtime (for ESP32 compatibility)..."
if python3 -m pip install --user tflite-runtime 2>&1 | grep -q "ERROR"; then
    echo "⚠️  tflite-runtime installation had issues, but continuing..."
else
    echo "✅ tflite-runtime installed"
fi

# Install openwakeword and numpy
echo ""
echo "Installing openwakeword and numpy..."
python3 -m pip install --user openwakeword numpy 2>&1 | tail -5

echo ""
echo "✅ Dependencies installed"
echo ""

# Check if models directory exists
MODELS_DIR="components/openwakeword/models"
if [ ! -d "$MODELS_DIR" ]; then
    echo "📥 Models directory not found. Would you like to download models?"
    echo "   Run: cd components/openwakeword && ./download_models.sh"
    echo ""
else
    echo "✅ Models directory exists: $MODELS_DIR"
    if [ -f "$MODELS_DIR/hey_jarvis_v0.1.tflite" ]; then
        echo "   ✓ hey_jarvis model found (for baseline testing)"
    fi
    echo ""
fi

# Check for custom hey_nap model
if [ -f "hey_nap.tflite" ] || [ -f "models/hey_nap.tflite" ] || [ -f "$MODELS_DIR/hey_nap.tflite" ]; then
    echo "✅ Custom 'hey_nap.tflite' model found!"
    echo ""
else
    echo "ℹ️  No custom 'hey_nap.tflite' model found yet."
    echo "   The test will use 'hey_jarvis' as a baseline."
    echo "   To train a custom model, see the instructions in test_hey_nap_local.py"
    echo ""
fi

echo "=========================================="
echo "Setup complete!"
echo "=========================================="
echo ""
echo "You can now run the test:"
echo "  python3 test_hey_nap_local.py"
echo ""
echo "Or with options:"
echo "  python3 test_hey_nap_local.py --help"
echo ""
