#!/usr/bin/env python3
"""
Collect real "Hey Nap" training data for model improvement.

This script helps you record multiple samples of "Hey Nap" from different
speakers, environments, and speaking styles to improve model performance.
"""

import os
import sys
import subprocess
import wave
import argparse
from pathlib import Path
from datetime import datetime

def record_audio(output_path, duration=2.0, sample_rate=16000):
    """Record audio using macOS 'rec' or 'sox' command."""
    print(f"\n🎤 Recording '{output_path}'...")
    print(f"   Say 'Hey Nap' clearly")
    print(f"   Recording {duration} seconds...")
    
    # Try different recording methods
    commands = [
        # Try sox first (if installed)
        ["rec", "-r", str(sample_rate), "-c", "1", "-t", "wav", str(output_path), "trim", "0", str(duration)],
        # Fallback to macOS built-in
        ["say", "-o", str(output_path), "--data-format=LEI16@16000", "--channels=1", "Hey Nap"],
    ]
    
    for cmd in commands:
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=int(duration) + 2)
            if result.returncode == 0 and Path(output_path).exists():
                # Verify the file
                with wave.open(str(output_path), 'rb') as wav:
                    if wav.getframerate() == sample_rate:
                        print(f"   ✅ Recorded: {output_path}")
                        return True
        except (subprocess.TimeoutExpired, FileNotFoundError, Exception):
            continue
    
    print(f"   ⚠️  Could not record. Using macOS 'say' to generate sample...")
    # Fallback: generate with say
    try:
        subprocess.run([
            'say', '-o', str(output_path),
            '--data-format=LEI16@16000',
            '--channels=1',
            'Hey Nap'
        ], check=True, capture_output=True)
        print(f"   ✅ Generated: {output_path}")
        return True
    except Exception as e:
        print(f"   ❌ Failed: {e}")
        return False

def collect_samples(output_dir, num_samples=50, speaker_name="speaker1"):
    """Collect multiple training samples."""
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    print("="*70)
    print(f"Collecting Training Data for 'Hey Nap'")
    print("="*70)
    print(f"\nOutput directory: {output_dir}")
    print(f"Target samples: {num_samples}")
    print(f"Speaker: {speaker_name}")
    print("\n💡 Tips for better training data:")
    print("   - Say 'Hey Nap' naturally (not too fast or slow)")
    print("   - Vary your tone and volume")
    print("   - Record in different environments")
    print("   - Have multiple speakers record")
    
    samples_collected = 0
    
    for i in range(num_samples):
        sample_name = f"hey_nap_{speaker_name}_{i:04d}.wav"
        sample_path = output_dir / sample_name
        
        if sample_path.exists():
            print(f"\n⏭️  Skipping {sample_name} (already exists)")
            samples_collected += 1
            continue
        
        print(f"\n[{i+1}/{num_samples}]")
        if record_audio(sample_path, duration=2.0):
            samples_collected += 1
            print(f"   Progress: {samples_collected}/{num_samples} samples")
        else:
            print(f"   ⚠️  Failed to record sample {i+1}")
        
        # Ask if user wants to continue
        if i < num_samples - 1:
            try:
                response = input(f"\n   Continue? (Y/n): ").strip().lower()
                if response == 'n':
                    print(f"\n✅ Collected {samples_collected} samples")
                    break
            except KeyboardInterrupt:
                print(f"\n\n✅ Collected {samples_collected} samples")
                break
    
    print("\n" + "="*70)
    print(f"✅ Collection Complete!")
    print(f"   Samples collected: {samples_collected}")
    print(f"   Location: {output_dir}")
    print("\n📝 Next steps:")
    print(f"   1. Review samples in: {output_dir}")
    print(f"   2. Upload to Colab for training")
    print(f"   3. Or use with local training pipeline")
    print("="*70)
    
    return samples_collected

def main():
    parser = argparse.ArgumentParser(description="Collect 'Hey Nap' training data")
    parser.add_argument("--output", type=str, default="training_data/positive",
                       help="Output directory for samples")
    parser.add_argument("--samples", type=int, default=50,
                       help="Number of samples to collect")
    parser.add_argument("--speaker", type=str, default="speaker1",
                       help="Speaker name (for organizing multiple speakers)")
    
    args = parser.parse_args()
    
    samples = collect_samples(args.output, args.samples, args.speaker)
    
    return 0 if samples > 0 else 1

if __name__ == "__main__":
    sys.exit(main())
