#!/usr/bin/env python3
"""
Train a custom "Hey Nap" wake word model for ESP32.

This script provides a simplified approach to training a custom wake word model.
For best results, use the Google Colab notebook mentioned in the OpenWakeWord docs.

Requirements:
- PyTorch
- openwakeword
- numpy
- scipy
- sklearn (for verifier model)

Note: Full model training requires GPU and complex setup. This script provides
a simpler verifier model approach that works with minimal data.
"""

import os
import sys
import subprocess
import wave
import argparse
from pathlib import Path
import numpy as np

def generate_training_samples(output_dir, phrase="Hey, Nap", count=50, sample_rate=16000):
    """Generate training samples using macOS 'say' command."""
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    print(f"🎤 Generating {count} training samples for '{phrase}'...")
    
    generated = 0
    for i in range(count):
        output_file = output_dir / f"hey_nap_{i:04d}.wav"
        
        # Use say command with slight variations
        cmd = [
            'say',
            '-o', str(output_file),
            '--data-format=LEI16@{}'.format(sample_rate),
            '--channels=1',
            phrase
        ]
        
        try:
            subprocess.run(cmd, capture_output=True, check=True)
            if output_file.exists():
                generated += 1
        except Exception as e:
            print(f"   ⚠️  Failed to generate sample {i}: {e}")
    
    print(f"✅ Generated {generated}/{count} samples in {output_dir}")
    return generated

def train_custom_verifier_model(positive_clips_dir, negative_clips_dir, output_path):
    """Train a custom verifier model (simpler approach)."""
    try:
        from openwakeword import train_custom_verifier
        from openwakeword import Model
    except ImportError:
        print("❌ openwakeword not installed. Install with: pip install openwakeword")
        return False
    
    # Find positive clips
    positive_dir = Path(positive_clips_dir)
    positive_clips = list(positive_dir.glob("*.wav"))
    
    if len(positive_clips) < 3:
        print(f"❌ Need at least 3 positive clips, found {len(positive_clips)}")
        return False
    
    # Find negative clips
    negative_dir = Path(negative_clips_dir)
    negative_clips = list(negative_dir.glob("*.wav"))
    
    if len(negative_clips) < 5:
        print(f"⚠️  Warning: Only {len(negative_clips)} negative clips found. More is better.")
        print("   Generating some negative samples...")
        # Generate some negative samples
        negative_phrases = [
            "Hello world",
            "What time is it",
            "How are you",
            "Good morning",
            "Thank you",
            "Please help",
            "Turn on lights",
            "Play music"
        ]
        for i, phrase in enumerate(negative_phrases[:max(5, len(negative_clips))]):
            output_file = negative_dir / f"negative_{i:04d}.wav"
            cmd = [
                'say',
                '-o', str(output_file),
                '--data-format=LEI16@16000',
                '--channels=1',
                phrase
            ]
            try:
                subprocess.run(cmd, capture_output=True, check=True)
                if output_file.exists():
                    negative_clips.append(output_file)
            except:
                pass
    
    print(f"\n📦 Training custom verifier model...")
    print(f"   Positive clips: {len(positive_clips)}")
    print(f"   Negative clips: {len(negative_clips)}")
    
    try:
        # Explicitly specify the model to avoid loading all models
        train_custom_verifier(
            positive_reference_clips=[str(p) for p in positive_clips[:10]],  # Use up to 10
            negative_reference_clips=[str(n) for n in negative_clips[:20]],  # Use up to 20
            output_path=output_path,
            model_name="hey_jarvis",  # Base model
            wakeword_models=["hey_jarvis"],  # Explicitly specify to avoid loading all models
            inference_framework="onnx"  # Use ONNX for training
        )
        print(f"✅ Custom verifier model saved to: {output_path}")
        print("\n⚠️  Note: This creates a verifier model (.pkl), not a standalone TFLite model.")
        print("   For a full TFLite model, use the full training pipeline (see instructions below).")
        return True
    except Exception as e:
        print(f"❌ Training failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    parser = argparse.ArgumentParser(description="Train 'Hey Nap' wake word model")
    parser.add_argument("--generate-data", action="store_true", 
                       help="Generate training data using macOS 'say' command")
    parser.add_argument("--positive-count", type=int, default=50,
                       help="Number of positive samples to generate (default: 50)")
    parser.add_argument("--train-verifier", action="store_true",
                       help="Train a custom verifier model (simpler, requires base model)")
    parser.add_argument("--positive-dir", type=str, default="training_data/positive",
                       help="Directory for positive training samples")
    parser.add_argument("--negative-dir", type=str, default="training_data/negative",
                       help="Directory for negative training samples")
    parser.add_argument("--output", type=str, default="hey_nap_verifier.pkl",
                       help="Output path for trained model")
    args = parser.parse_args()
    
    print("="*70)
    print("Hey Nap Wake Word Model Training")
    print("="*70)
    print()
    
    if args.generate_data:
        print("1️⃣  Generating training data...")
        positive_dir = Path(args.positive_dir)
        positive_dir.mkdir(parents=True, exist_ok=True)
        
        count = generate_training_samples(
            positive_dir,
            phrase="Hey, Nap",
            count=args.positive_count
        )
        
        if count < 3:
            print("❌ Failed to generate enough samples")
            return 1
        
        print(f"\n✅ Generated {count} positive samples")
        print(f"   Location: {positive_dir.absolute()}")
        print()
    
    if args.train_verifier:
        print("2️⃣  Training custom verifier model...")
        
        # Ensure negative directory exists
        negative_dir = Path(args.negative_dir)
        negative_dir.mkdir(parents=True, exist_ok=True)
        
        success = train_custom_verifier_model(
            args.positive_dir,
            args.negative_dir,
            args.output
        )
        
        if success:
            print(f"\n✅ Training complete!")
            print(f"   Model saved to: {Path(args.output).absolute()}")
            print()
            print("📝 Next steps:")
            print("   1. Test the model with: python3 test_hey_nap_local.py")
            print("   2. For ESP32, you'll need a full TFLite model (see instructions below)")
        else:
            print("\n❌ Training failed")
            return 1
    
    if not args.generate_data and not args.train_verifier:
        print("ℹ️  No action specified. Use --help to see options.")
        print()
        print("Quick start:")
        print("  1. Generate training data:")
        print("     python3 train_hey_nap_model.py --generate-data")
        print()
        print("  2. Train verifier model:")
        print("     python3 train_hey_nap_model.py --train-verifier")
        print()
        print("  3. Or do both:")
        print("     python3 train_hey_nap_model.py --generate-data --train-verifier")
        print()
        print("="*70)
        print("For Full TFLite Model Training:")
        print("="*70)
        print()
        print("The custom verifier model (.pkl) works with a base model.")
        print("For a standalone TFLite model, you need the full training pipeline:")
        print()
        print("Option 1: Use Google Colab (Recommended)")
        print("  - See: https://colab.research.google.com/drive/1q1oe2zOyZp7UsB3jJiQ1IFn8z5YfjwEb")
        print("  - This is the easiest way to train a full model")
        print()
        print("Option 2: Full Local Training")
        print("  - Requires: PyTorch, GPU, TTS models (Piper), large datasets")
        print("  - See: components/openwakeword/lib/README.md")
        print("  - See: components/openwakeword/lib/openwakeword/train.py")
        print("  - This is complex and requires significant setup")
        print()
        print("The verifier model approach (this script) is simpler but requires")
        print("a base model (like 'hey_jarvis') to work with.")
        print("="*70)
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
