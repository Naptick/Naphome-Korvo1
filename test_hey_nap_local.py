#!/usr/bin/env python3
"""
Test "Hey Nap" wake word detection locally on macOS.

This script tests with the same model format and processing approach as ESP32-S3 Korvo1:
- Uses TFLite models (ESP32 format)
- Processes audio in 512-sample chunks (32ms at 16kHz) matching ESP32
- Accumulates to 1280 samples for OpenWakeWord inference
- Simulates ESP32's buffering and chunking behavior

This script:
1. Generates "Hey Nap" audio using macOS 'say' command
2. Tests detection with OpenWakeWord using ESP32-compatible setup
3. Supports custom "hey_nap.tflite" model if available
4. Can test with WAV files
"""

import os
import sys
import subprocess
import wave
import argparse
from pathlib import Path

# Try to import openwakeword
try:
    from openwakeword import Model
    import numpy as np
    OPENWAKEWORD_AVAILABLE = True
    
    # Check for inference frameworks
    HAS_TFLITE = False
    HAS_ONNX = False
    try:
        import tflite_runtime.interpreter as tflite
        HAS_TFLITE = True
    except ImportError:
        pass
    
    try:
        import onnxruntime
        HAS_ONNX = True
    except ImportError:
        pass
    
    if not HAS_TFLITE and not HAS_ONNX:
        print("⚠️  Warning: No inference framework found. Install one of:")
        print("   - pip install tflite-runtime  (for TFLite)")
        print("   - pip install onnxruntime    (for ONNX)")
        print("   - pip install openwakeword[onnx]  (recommended)")
        
except ImportError:
    OPENWAKEWORD_AVAILABLE = False
    HAS_TFLITE = False
    HAS_ONNX = False
    print("⚠️  openwakeword not installed. Install with: pip install openwakeword numpy")
    print("   Will generate WAV file only.")

def generate_hey_nap_wav(output_path, sample_rate=16000):
    """Generate WAV using macOS 'say' command."""
    output_path = Path(output_path)
    
    print(f"🎤 Generating 'Hey, Nap' audio using macOS 'say' command...")
    
    cmd = [
        'say',
        '-o', str(output_path),
        '--data-format=LEI16@16000',
        '--channels=1',
        'Hey, Nap'
    ]
    
    try:
        subprocess.run(cmd, capture_output=True, text=True, check=True)
        print(f"✅ Generated WAV: {output_path}")
        
        if output_path.exists():
            with wave.open(str(output_path), 'rb') as wav:
                print(f"   Sample rate: {wav.getframerate()} Hz")
                print(f"   Channels: {wav.getnchannels()}")
                print(f"   Duration: {wav.getnframes() / wav.getframerate():.2f} seconds")
            return True
        return False
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print(f"❌ Failed to generate WAV: {e}")
        return False

def find_hey_nap_model():
    """Search for a custom 'hey_nap' model file. Prefer TFLite for ESP32 compatibility."""
    # ESP32 uses TFLite, so prioritize TFLite models
    # Check in order: project root, models/, ESP32 models directory
    possible_paths = [
        "hey_nap.tflite",  # TFLite (ESP32 format) - highest priority
        "models/hey_nap.tflite",
        "components/openwakeword/models/hey_nap.tflite",
        "components/openwakeword/lib/openwakeword/resources/models/hey_nap.tflite",
        "hey_nap.onnx",  # ONNX fallback
        "models/hey_nap.onnx",
        "components/openwakeword/models/hey_nap.onnx",
        "components/openwakeword/lib/openwakeword/resources/models/hey_nap.onnx",
    ]
    
    for path in possible_paths:
        if os.path.exists(path):
            abs_path = os.path.abspath(path)
            print(f"   Found model at: {abs_path}")
            return abs_path
    return None

def test_wake_word_detection(wav_path, model_path=None, threshold=0.5):
    """Test wake word detection on WAV file."""
    if not OPENWAKEWORD_AVAILABLE:
        print("❌ OpenWakeWord not available. Cannot test detection.")
        print("   Install with: pip install openwakeword numpy")
        return False
    
    print(f"\n🔍 Testing wake word detection on: {wav_path}")
    
    # Load WAV file
    with wave.open(str(wav_path), 'rb') as wav:
        sample_rate = wav.getframerate()
        n_channels = wav.getnchannels()
        frames = wav.readframes(-1)
        audio_data = np.frombuffer(frames, dtype=np.int16)
        
        if n_channels == 2:
            audio_data = audio_data[::2]  # Convert to mono
        
        print(f"   Sample rate: {sample_rate} Hz")
        print(f"   Duration: {len(audio_data) / sample_rate:.2f} seconds")
        print(f"   Samples: {len(audio_data)}")
    
    # Determine which model to use
    # ESP32 uses TFLite models, so prioritize TFLite format
    model_to_test = None
    model_name = None
    
    if model_path and os.path.exists(model_path):
        print(f"\n📦 Loading custom model: {model_path}")
        model_to_test = [model_path]
        model_name = "hey_nap"
    else:
        # Check for custom model in common locations (prefer TFLite for ESP32 compatibility)
        custom_model = find_hey_nap_model()
        if custom_model:
            print(f"\n📦 Found custom 'hey_nap' model: {custom_model}")
            model_to_test = [custom_model]
            model_name = "hey_nap"
        else:
            print("\n📦 No custom 'hey_nap' model found.")
            print("   Testing with 'hey_jarvis' model as baseline (won't detect 'Hey Nap' correctly)")
            print("   To test with a custom model:")
            print("   1. Train a custom model (see instructions below)")
            print("   2. Place it as 'hey_nap.tflite' in this directory (TFLite format for ESP32)")
            model_to_test = ["hey_jarvis"]
            model_name = "hey_jarvis"
    
    # Initialize OpenWakeWord model
    # ESP32 uses TFLite, so prioritize TFLite framework
    print("\n📦 Loading OpenWakeWord model (using TFLite to match ESP32)...")
    
    # Determine inference framework - ESP32 uses TFLite
    if model_to_test[0].endswith(".onnx"):
        inference_framework = "onnx"
        print("   ⚠️  Warning: ONNX model detected, but ESP32 uses TFLite")
        print("   Consider converting to TFLite for accurate ESP32 testing")
    elif model_to_test[0].endswith(".tflite"):
        inference_framework = "tflite"
    elif not os.path.exists(model_to_test[0]):
        # Pre-trained model name - use TFLite (ESP32 format)
        inference_framework = "tflite"
    else:
        inference_framework = "tflite"  # Default to TFLite for ESP32 compatibility
    
    oww_model = None
    
    # Try loading with TFLite first (ESP32 format), fallback to ONNX if needed
    frameworks_to_try = ["tflite", "onnx"] if inference_framework == "tflite" else [inference_framework]
    
    for framework in frameworks_to_try:
        try:
            # For pre-trained model names, OpenWakeWord will handle path resolution
            # For file paths, use them directly
            oww_model = Model(
                wakeword_models=model_to_test,
                inference_framework=framework
            )
            print(f"✅ Model loaded ({framework})")
            if framework == "tflite":
                print("   ✓ Using TFLite (matches ESP32 deployment)")
            else:
                print("   ⚠️  Using ONNX (ESP32 uses TFLite - results may differ)")
            inference_framework = framework
            break
        except Exception as e:
            error_msg = str(e)
            if framework == "tflite":
                if "tflite" in error_msg.lower() and ("not found" in error_msg.lower() or "import" in error_msg.lower()):
                    print(f"   ⚠️  TFLite runtime not available")
                    print("   Trying ONNX as fallback...")
                    continue
                else:
                    # Other TFLite errors - might be model path issue
                    print(f"   ⚠️  TFLite failed: {error_msg}")
                    if "Could not find pretrained model" in error_msg:
                        print("   Trying ONNX as fallback...")
                        continue
                    else:
                        print("   Trying ONNX as fallback...")
                        continue
            elif framework == "onnx":
                if "onnx" in error_msg.lower() and ("not found" in error_msg.lower() or "import" in error_msg.lower()):
                    print(f"   ❌ ONNX runtime also not available")
                    break
                else:
                    print(f"   ❌ ONNX also failed: {error_msg}")
                    break
            else:
                print(f"   ⚠️  {framework} failed: {error_msg}")
                continue
    
    if oww_model is None:
        print("\n❌ Failed to load model. Missing dependencies:")
        print("   For ESP32 compatibility, install TFLite runtime:")
        print("   - pip install tflite-runtime")
        print("\n   Or install ONNX runtime (fallback, not ESP32 format):")
        print("   - pip install onnxruntime")
        print("\n   Or install both:")
        print("   - pip install openwakeword[onnx]  (includes onnxruntime)")
        return False
    
    # Process audio in chunks matching ESP32 implementation
    # ESP32 processes in 512-sample chunks (32ms at 16kHz)
    # OpenWakeWord expects 1280 samples (80ms) for inference, but we'll simulate ESP32's chunking
    esp32_chunk_size = 512  # ESP32 chunk size
    oww_chunk_size = 1280   # OpenWakeWord inference chunk size
    detections = []
    all_scores = []
    max_score = 0.0
    
    # Buffer to accumulate ESP32-sized chunks into OpenWakeWord-sized chunks
    audio_buffer = np.array([], dtype=np.int16)
    inference_count = 0
    
    print(f"\n🎤 Processing audio (ESP32-style: {esp32_chunk_size}-sample chunks → {oww_chunk_size}-sample inference)...")
    print(f"   This simulates how ESP32 processes audio in {esp32_chunk_size}-sample chunks")
    
    # Process in ESP32-sized chunks, but accumulate for OpenWakeWord inference
    for i in range(0, len(audio_data), esp32_chunk_size):
        esp32_chunk = audio_data[i:i+esp32_chunk_size]
        
        # Pad ESP32 chunk if needed
        if len(esp32_chunk) < esp32_chunk_size:
            esp32_chunk = np.pad(esp32_chunk, (0, esp32_chunk_size - len(esp32_chunk)), mode='constant')
        
        # Accumulate into buffer (simulating ESP32 buffering)
        audio_buffer = np.concatenate([audio_buffer, esp32_chunk])
        
        # When we have enough samples for OpenWakeWord inference, run prediction
        while len(audio_buffer) >= oww_chunk_size:
            # Extract chunk for inference
            inference_chunk = audio_buffer[:oww_chunk_size]
            audio_buffer = audio_buffer[oww_chunk_size:]  # Keep remainder
            
            # Calculate time for this inference (based on samples processed)
            samples_processed = inference_count * oww_chunk_size
            time_sec = samples_processed / sample_rate
            inference_count += 1
            
            # Run OpenWakeWord prediction (same as ESP32 would do)
            prediction = oww_model.predict(inference_chunk)
            
            for pred_model_name, scores in prediction.items():
                # Handle both list and single float scores
                if isinstance(scores, (list, tuple)) and len(scores) > 0:
                    score = scores[0]
                elif isinstance(scores, (int, float)):
                    score = float(scores)
                else:
                    continue
                
                all_scores.append(score)
                if score > max_score:
                    max_score = score
                
                if score > threshold:
                    detections.append((time_sec, pred_model_name, score))
                    print(f"   ⚠️  Detection at {time_sec:.2f}s: {pred_model_name} (score: {score:.3f})")
    
    # Process any remaining audio in buffer
    if len(audio_buffer) > 0:
        # Pad to OWW chunk size for final inference
        final_chunk = np.pad(audio_buffer, (0, oww_chunk_size - len(audio_buffer)), mode='constant')
        time_sec = inference_count * oww_chunk_size / sample_rate
        prediction = oww_model.predict(final_chunk)
        
        for pred_model_name, scores in prediction.items():
            # Handle both list and single float scores
            if isinstance(scores, (list, tuple)) and len(scores) > 0:
                score = scores[0]
            elif isinstance(scores, (int, float)):
                score = float(scores)
            else:
                continue
            
            all_scores.append(score)
            if score > max_score:
                max_score = score
            
            if score > threshold:
                detections.append((time_sec, pred_model_name, score))
                print(f"   ⚠️  Detection at {time_sec:.2f}s: {pred_model_name} (score: {score:.3f})")
    
    # Print results
    print(f"\n📊 Results:")
    print(f"   Max score: {max_score:.3f}")
    if all_scores:
        print(f"   Average score: {np.mean(all_scores):.3f}")
        print(f"   Score range: [{np.min(all_scores):.3f}, {np.max(all_scores):.3f}]")
    print(f"   Detections (threshold {threshold}): {len(detections)}")
    
    if detections:
        print(f"\n✅ Found {len(detections)} detection(s)!")
        if model_name == "hey_nap":
            print("   🎉 Custom 'hey_nap' model is working!")
        else:
            print("   ⚠️  Note: Using 'hey_jarvis' model, so this may be a false positive")
            print("   For accurate 'Hey Nap' detection, train a custom model")
    else:
        print(f"\n❌ No detections above threshold {threshold}")
        if model_name == "hey_jarvis":
            print("   This is expected - 'Hey Nap' won't match 'hey_jarvis' model")
            print("   The pipeline works, but you need a custom 'hey_nap' model")
        else:
            print("   Try:")
            print("   - Lowering the threshold (currently {threshold})")
            print("   - Checking if the model was trained correctly")
            print("   - Verifying the audio quality")
        if max_score > 0.1:
            print(f"   However, max score {max_score:.3f} shows some similarity")
    
    return len(detections) > 0

def main():
    parser = argparse.ArgumentParser(description="Test 'Hey Nap' wake word detection locally")
    parser.add_argument("--wav", type=str, help="Path to WAV file (default: generate with 'say')")
    parser.add_argument("--model", type=str, help="Path to custom model file (.tflite or .onnx)")
    parser.add_argument("--threshold", type=float, default=0.5, help="Detection threshold (default: 0.5)")
    parser.add_argument("--no-generate", action="store_true", help="Don't generate WAV if it doesn't exist")
    args = parser.parse_args()
    
    print("="*70)
    print("Hey Nap Wake Word Detection Test (macOS)")
    print("Testing with ESP32-S3 Korvo1 compatible setup")
    print("="*70)
    
    # Determine WAV file path
    if args.wav:
        wav_path = Path(args.wav)
    else:
        wav_path = Path("hey_nap.wav")
    
    # Generate WAV if needed
    if not wav_path.exists() and not args.no_generate:
        print("\n1️⃣  Generating 'Hey, Nap' WAV file...")
        if not generate_hey_nap_wav(wav_path):
            print("\n❌ Failed to generate WAV file")
            return 1
    elif wav_path.exists():
        print(f"\n✅ Using existing WAV file: {wav_path}")
    else:
        print(f"\n❌ WAV file not found: {wav_path}")
        print("   Use --wav to specify a file or remove --no-generate to generate one")
        return 1
    
    # Test detection
    if OPENWAKEWORD_AVAILABLE:
        print("\n2️⃣  Testing wake word detection...")
        success = test_wake_word_detection(wav_path, args.model, args.threshold)
    else:
        print("\n2️⃣  Skipping detection test (openwakeword not installed)")
        print("   Install with: pip install openwakeword numpy")
        success = False
    
    # Summary
    print("\n" + "="*70)
    print("Summary:")
    print("="*70)
    print(f"✅ WAV file: {wav_path.absolute()}")
    
    if OPENWAKEWORD_AVAILABLE:
        if success:
            print("✅ Detection test completed successfully")
        else:
            print("⚠️  Detection test completed (no detections found)")
    else:
        print("⚠️  Detection test skipped (install openwakeword to test)")
        print("\n💡 Quick setup:")
        print("   Run: ./setup_test_environment.sh")
        print("   Or manually: pip install tflite-runtime openwakeword numpy")
    
    # Instructions for custom model
    if not find_hey_nap_model() and not args.model:
        print("\n📝 To train a custom 'Hey Nap' model for ESP32:")
        print("   1. Collect audio samples (16kHz, 16-bit, mono WAV):")
        print("      - 3+ examples of 'Hey Nap' (positive)")
        print("      - 10+ seconds of other speech (negative)")
        print("   2. Train the model (export as TFLite for ESP32):")
        print("      See: components/openwakeword/lib/README.md")
        print("      Or use: components/openwakeword/lib/openwakeword/train.py")
        print("   3. This will create 'hey_nap.tflite' (ESP32-compatible)")
        print("   4. Place the .tflite file in this directory or models/")
        print("   5. Run this test again to use the custom model")
    
    print("\n💡 For ESP32 deployment:")
    print("   - Models should be in TFLite format (.tflite)")
    print("   - Place in: components/openwakeword/models/")
    print("   - See: components/openwakeword/INTEGRATION.md")
    print("="*70)
    
    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
