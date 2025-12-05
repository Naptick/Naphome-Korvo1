#!/usr/bin/env python3
"""
Full training script for "Hey Nap" TFLite model.

This script wraps the OpenWakeWord training pipeline to make it easier to use.
For best results, use the Google Colab notebook instead.

Requirements:
- PyTorch
- TTS models (Piper) for synthetic data generation
- Large negative datasets (optional but recommended)
- GPU (recommended for faster training)
"""

import os
import sys
import subprocess
import argparse
from pathlib import Path

def check_dependencies():
    """Check if required dependencies are installed."""
    missing = []
    
    try:
        import torch
        print(f"✅ PyTorch: {torch.__version__}")
        if torch.cuda.is_available():
            print(f"   CUDA available: {torch.cuda.get_device_name(0)}")
        else:
            print("   ⚠️  CUDA not available (training will be slower on CPU)")
    except ImportError:
        missing.append("torch")
        print("❌ PyTorch not installed")
    
    try:
        import openwakeword
        print("✅ openwakeword installed")
    except ImportError:
        missing.append("openwakeword")
        print("❌ openwakeword not installed")
    
    try:
        import yaml
        print("✅ yaml installed")
    except ImportError:
        missing.append("pyyaml")
        print("❌ pyyaml not installed")
    
    if missing:
        print(f"\n❌ Missing dependencies: {', '.join(missing)}")
        print("   Run: ./setup_full_training.sh")
        return False
    
    return True

def run_training(config_path, generate_clips=False, augment_clips=False, train_model=False, overwrite=False):
    """Run the OpenWakeWord training pipeline."""
    
    if not os.path.exists(config_path):
        print(f"❌ Config file not found: {config_path}")
        print("   Create training_config_hey_nap.yaml first")
        return False
    
    # Get the training script path
    train_script = Path(__file__).parent / "components" / "openwakeword" / "lib" / "openwakeword" / "train.py"
    
    if not train_script.exists():
        print(f"❌ Training script not found: {train_script}")
        return False
    
    # Build command
    cmd = [
        sys.executable,
        str(train_script),
        "--training_config", config_path
    ]
    
    if generate_clips:
        cmd.append("--generate_clips")
    if augment_clips:
        cmd.append("--augment_clips")
    if train_model:
        cmd.append("--train_model")
    if overwrite:
        cmd.append("--overwrite")
    
    print(f"\n🚀 Running training pipeline...")
    print(f"   Command: {' '.join(cmd)}")
    print()
    
    # Run the training
    try:
        result = subprocess.run(cmd, check=False)
        return result.returncode == 0
    except Exception as e:
        print(f"❌ Training failed: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description="Train full 'Hey Nap' TFLite model")
    parser.add_argument("--config", type=str, default="training_config_hey_nap.yaml",
                       help="Path to training config file")
    parser.add_argument("--generate-clips", action="store_true",
                       help="Generate synthetic training clips using TTS")
    parser.add_argument("--augment-clips", action="store_true",
                       help="Augment clips with background noise and RIR")
    parser.add_argument("--train-model", action="store_true",
                       help="Train the model")
    parser.add_argument("--all", action="store_true",
                       help="Run all steps (generate, augment, train)")
    parser.add_argument("--overwrite", action="store_true",
                       help="Overwrite existing features")
    parser.add_argument("--check-deps", action="store_true",
                       help="Check dependencies and exit")
    
    args = parser.parse_args()
    
    print("="*70)
    print("Full TFLite Model Training for 'Hey Nap'")
    print("="*70)
    print()
    
    # Check dependencies
    if not check_dependencies():
        if not args.check_deps:
            print("\n💡 Install dependencies with: ./setup_full_training.sh")
        return 1
    
    if args.check_deps:
        print("\n✅ All dependencies installed!")
        return 0
    
    # Determine which steps to run
    if args.all:
        generate_clips = True
        augment_clips = True
        train_model = True
    else:
        generate_clips = args.generate_clips
        augment_clips = args.augment_clips
        train_model = args.train_model
    
    if not (generate_clips or augment_clips or train_model):
        print("ℹ️  No steps specified. Use --help to see options.")
        print()
        print("Quick start:")
        print("  # Run all steps:")
        print("  python3 train_full_hey_nap.py --all")
        print()
        print("  # Or run steps individually:")
        print("  python3 train_full_hey_nap.py --generate-clips")
        print("  python3 train_full_hey_nap.py --augment-clips")
        print("  python3 train_full_hey_nap.py --train-model")
        print()
        print("⚠️  Note: Full training requires:")
        print("  - TTS models (Piper) configured in config file")
        print("  - Large negative datasets (optional)")
        print("  - GPU recommended for faster training")
        print()
        print("💡 Easier option: Use Google Colab notebook")
        print("   https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb")
        return 0
    
    # Check config file
    if not os.path.exists(args.config):
        print(f"❌ Config file not found: {args.config}")
        print("   Create it from training_config_hey_nap.yaml template")
        return 1
    
    # Run training
    success = run_training(
        args.config,
        generate_clips=generate_clips,
        augment_clips=augment_clips,
        train_model=train_model,
        overwrite=args.overwrite
    )
    
    if success:
        print("\n" + "="*70)
        print("✅ Training Complete!")
        print("="*70)
        print()
        print("📦 Model should be in:")
        print("   models/hey_nap/hey_nap.tflite")
        print()
        print("🧪 Test the model:")
        print("   python3 test_hey_nap_local.py --model models/hey_nap/hey_nap.tflite")
        print()
    else:
        print("\n" + "="*70)
        print("❌ Training Failed")
        print("="*70)
        print()
        print("💡 Troubleshooting:")
        print("   1. Check config file: training_config_hey_nap.yaml")
        print("   2. Ensure TTS models (Piper) are configured")
        print("   3. Check that datasets are available")
        print("   4. See error messages above for details")
        print()
        print("   Or use Google Colab (easier):")
        print("   https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb")
        return 1
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
