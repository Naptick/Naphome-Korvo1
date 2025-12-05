#!/bin/bash
# Setup script for full TFLite model training

set -e

echo "=========================================="
echo "Setting up Full TFLite Model Training"
echo "=========================================="
echo ""

# Check Python
if ! command -v python3 &> /dev/null; then
    echo "❌ Python 3 not found"
    exit 1
fi

echo "✅ Python 3 found: $(python3 --version)"
echo ""

# Install PyTorch
echo "📦 Installing PyTorch..."
echo "   This may take a few minutes..."
python3 -m pip install torch torchvision torchaudio 2>&1 | tail -5

# Check if CUDA is available
python3 -c "import torch; print(f'PyTorch: {torch.__version__}'); print(f'CUDA available: {torch.cuda.is_available()}')" 2>&1

# Install other dependencies
echo ""
echo "📦 Installing training dependencies..."
python3 -m pip install openwakeword numpy scipy scikit-learn pyyaml tqdm 2>&1 | tail -5

# Install ONNX to TFLite converter dependencies
echo ""
echo "📦 Installing model conversion dependencies..."
python3 -m pip install onnx onnx-tf tensorflow 2>&1 | tail -5 || echo "⚠️  Some conversion dependencies may not be available"

echo ""
echo "=========================================="
echo "Setup Complete!"
echo "=========================================="
echo ""
echo "⚠️  Additional Requirements:"
echo ""
echo "1. TTS Models (Piper):"
echo "   - Install Piper TTS: https://github.com/rhasspy/piper"
echo "   - Update 'piper_sample_generator_path' in training_config_hey_nap.yaml"
echo ""
echo "2. Negative Datasets (Optional but recommended):"
echo "   - Download large audio datasets for negative examples"
echo "   - Update 'background_paths' and 'rir_paths' in config"
echo ""
echo "3. Validation Data:"
echo "   - Prepare false positive validation dataset"
echo "   - Update 'false_positive_validation_data_path' in config"
echo ""
echo "📝 Next Steps:"
echo "   1. Configure training_config_hey_nap.yaml"
echo "   2. Run: python3 train_full_hey_nap.py --generate-clips"
echo "   3. Run: python3 train_full_hey_nap.py --augment-clips"
echo "   4. Run: python3 train_full_hey_nap.py --train-model"
echo ""
echo "   Or use the Google Colab notebook (easier):"
echo "   https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb"
echo ""
