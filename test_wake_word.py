#!/usr/bin/env python3
"""
Test script to generate "Hey, Nap" audio and test OpenWakeWord detection locally.
Uses macOS 'say' command to generate WAV (no dependencies required).
"""

import os
import sys
import subprocess
import wave
from pathlib import Path

# Try to import openwakeword
try:
    from openwakeword import Model
    import numpy as np
    OPENWAKEWORD_AVAILABLE = True
except ImportError:
    OPENWAKEWORD_AVAILABLE = False
    print("⚠️  openwakeword not installed. Install with: pip install openwakeword numpy")
    print("   Will generate WAV file only.")

def generate_hey_nap_wav_say(output_path, sample_rate=16000):
    """Generate WAV using macOS 'say' command (no dependencies)."""
    output_path = Path(output_path)
    
    print(f"🎤 Generating 'Hey, Nap' audio using macOS 'say' command...")
    
    # macOS 'say' command with WAV output
    # Format: LEI16 = Little Endian, Integer, 16-bit
    cmd = [
        'say',
        '-o', str(output_path),
        '--data-format=LEI16@16000',
        '--channels=1',
        'Hey, Nap'
    ]
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        print(f"✅ Generated WAV: {output_path}")
        
        # Verify the file
        if output_path.exists():
            with wave.open(str(output_path), 'rb') as wav:
                print(f"   Sample rate: {wav.getframerate()} Hz")
                print(f"   Channels: {wav.getnchannels()}")
                print(f"   Sample width: {wav.getsampwidth()} bytes")
                print(f"   Duration: {wav.getnframes() / wav.getframerate():.2f} seconds")
            return True
        else:
            print(f"❌ File not created: {output_path}")
            return False
    except subprocess.CalledProcessError as e:
        print(f"❌ Failed to generate WAV: {e}")
        print(f"   Command: {' '.join(cmd)}")
        return False
    except FileNotFoundError:
        print("❌ 'say' command not found. Are you on macOS?")
        return False

def test_openwakeword(wav_path):
    """Test OpenWakeWord detection on WAV file."""
    if not OPENWAKEWORD_AVAILABLE:
        print("❌ OpenWakeWord not available. Cannot test detection.")
        print("   Install with: pip install openwakeword numpy")
        return False
    
    print(f"\n🔍 Testing OpenWakeWord detection on: {wav_path}")
    
    # Load WAV file
    with wave.open(str(wav_path), 'rb') as wav:
        sample_rate = wav.getframerate()
        n_channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        frames = wav.readframes(-1)
        audio_data = np.frombuffer(frames, dtype=np.int16)
        
        # Convert to mono if stereo
        if n_channels == 2:
            audio_data = audio_data[::2]  # Take left channel
        
        print(f"   Sample rate: {sample_rate} Hz")
        print(f"   Channels: {n_channels}")
        print(f"   Duration: {len(audio_data) / sample_rate:.2f} seconds")
        print(f"   Samples: {len(audio_data)}")
    
    # Initialize OpenWakeWord model
    # Note: OpenWakeWord doesn't have a "hey_nap" model, so we'll test with "hey_jarvis"
    # as a baseline to verify the detection pipeline works
    print("\n📦 Loading OpenWakeWord models...")
    try:
        # Load all available models or just "hey_jarvis"
        oww_model = Model(
            wakeword_models=["hey_jarvis"],  # Use hey_jarvis as test
            inference_framework="tflite"
        )
        print("✅ Models loaded")
    except Exception as e:
        print(f"❌ Failed to load models: {e}")
        print("   Make sure models are downloaded:")
        print("   python -c 'from openwakeword import Model; Model()'")
        return False
    
    # Process audio in chunks (OpenWakeWord expects 1280 samples = 80ms at 16kHz)
    chunk_size = 1280
    detections = []
    
    print(f"\n🎤 Processing audio in {chunk_size}-sample chunks...")
    for i in range(0, len(audio_data), chunk_size):
        chunk = audio_data[i:i+chunk_size]
        
        # Pad if needed
        if len(chunk) < chunk_size:
            chunk = np.pad(chunk, (0, chunk_size - len(chunk)), mode='constant')
        
        # Get predictions
        prediction = oww_model.predict(chunk)
        
        # Check for detections
        for model_name, scores in prediction.items():
            if len(scores) > 0 and scores[0] > 0.5:  # Threshold
                detections.append((i / sample_rate, model_name, scores[0]))
                print(f"   ⚠️  Detection at {i/sample_rate:.2f}s: {model_name} (score: {scores[0]:.3f})")
    
    if detections:
        print(f"\n✅ Found {len(detections)} detection(s) in audio")
        print("   Note: This is testing with 'hey_jarvis' model, not 'hey_nap'")
        print("   For 'Hey, Nap', you would need to train a custom model.")
    else:
        print("\n❌ No detections found")
        print("   This is expected since we're using 'hey_jarvis' model for 'Hey, Nap' audio")
        print("   The detection pipeline is working, but you need a custom 'hey_nap' model")
    
    return len(detections) > 0

def main():
    print("="*60)
    print("OpenWakeWord Test Script - 'Hey, Nap' Detection")
    print("="*60)
    
    wav_path = Path("hey_nap.wav")
    
    # Generate WAV file
    print("\n1️⃣  Generating 'Hey, Nap' WAV file...")
    if not wav_path.exists():
        if not generate_hey_nap_wav_say(wav_path):
            print("\n❌ Failed to generate WAV file")
            return 1
    else:
        print(f"✅ WAV file already exists: {wav_path}")
    
    if not wav_path.exists():
        print("\n❌ WAV file not found. Cannot continue.")
        return 1
    
    # Test with OpenWakeWord
    if OPENWAKEWORD_AVAILABLE:
        print("\n2️⃣  Testing OpenWakeWord detection...")
        test_openwakeword(wav_path)
    else:
        print("\n2️⃣  Skipping OpenWakeWord test (library not installed)")
        print("   Install with: pip install openwakeword numpy")
        print(f"\n✅ WAV file created: {wav_path}")
        print("   You can now:")
        print("   1. Play this file to test on ESP32")
        print("   2. Install openwakeword to test detection locally")
    
    print("\n" + "="*60)
    print("Summary:")
    print("="*60)
    print(f"✅ WAV file: {wav_path.absolute()}")
    if OPENWAKEWORD_AVAILABLE:
        print("✅ OpenWakeWord test completed")
    else:
        print("⚠️  OpenWakeWord not tested (install to test)")
    print("\nNote: OpenWakeWord doesn't have a 'hey_nap' model.")
    print("      The ESP32 currently uses energy-based detection.")
    print("      For actual 'Hey, Nap' detection, train a custom model.")
    print("="*60)
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
