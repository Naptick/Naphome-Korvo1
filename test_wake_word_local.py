#!/usr/bin/env python3
"""
Test OpenWakeWord detection locally on Mac with "Hey, Nap" audio.
"""

import subprocess
import sys
from pathlib import Path

# First, generate the WAV file using macOS 'say' command
wav_path = Path("hey_nap.wav")

print("="*60)
print("Testing OpenWakeWord Detection Locally")
print("="*60)

# Step 1: Generate WAV file
print("\n1️⃣  Generating 'Hey, Nap' WAV file...")
if not wav_path.exists():
    cmd = ['say', '-o', str(wav_path), '--data-format=LEI16@16000', '--channels=1', 'Hey, Nap']
    try:
        subprocess.run(cmd, check=True, capture_output=True)
        print(f"✅ Generated: {wav_path}")
    except Exception as e:
        print(f"❌ Failed to generate WAV: {e}")
        sys.exit(1)
else:
    print(f"✅ WAV file exists: {wav_path}")

# Step 2: Test with OpenWakeWord
print("\n2️⃣  Testing with OpenWakeWord...")
print("   (This will download models on first run)")

test_script = """
import wave
import numpy as np
from openwakeword import Model

# Load WAV
wav_path = '{wav_path}'
with wave.open(wav_path, 'rb') as wav:
    sample_rate = wav.getframerate()
    frames = wav.readframes(-1)
    audio_data = np.frombuffer(frames, dtype=np.int16)
    if wav.getnchannels() == 2:
        audio_data = audio_data[::2]  # Convert to mono

print(f"   Loaded audio: {{len(audio_data)}} samples, {{len(audio_data)/sample_rate:.2f}}s @ {{sample_rate}}Hz")

# Load OpenWakeWord model - try ONNX if TFLite fails
print("   Loading OpenWakeWord models (this may take a moment on first run)...")
try:
    oww_model = Model(wakeword_models=["hey_jarvis"], inference_framework="onnx")
    print("   ✅ Models loaded (ONNX)")
except Exception as e1:
    print(f"   ONNX failed: {{e1}}")
    try:
        oww_model = Model(wakeword_models=["hey_jarvis"], inference_framework="tflite")
        print("   ✅ Models loaded (TFLite)")
    except Exception as e2:
        print(f"   TFLite also failed: {{e2}}")
        raise

# Process audio
chunk_size = 1280  # 80ms at 16kHz
detections = []
max_score = 0.0
all_scores = []

print(f"   Processing audio in {{chunk_size}}-sample chunks...")
for i in range(0, len(audio_data), chunk_size):
    chunk = audio_data[i:i+chunk_size]
    if len(chunk) < chunk_size:
        chunk = np.pad(chunk, (0, chunk_size - len(chunk)), mode='constant')
    
    prediction = oww_model.predict(chunk)
    
    for model_name, scores in prediction.items():
        if len(scores) > 0:
            score = scores[0]
            all_scores.append(score)
            if score > max_score:
                max_score = score
            if score > 0.5:
                detections.append((i / sample_rate, model_name, score))
                print(f"      ⚠️  Detection at {{i/sample_rate:.2f}}s: {{model_name}} (score: {{score:.3f}})")

print(f"\\n   Results:")
print(f"      Max score: {{max_score:.3f}}")
print(f"      Average score: {{np.mean(all_scores):.3f}}")
print(f"      Detections (threshold 0.5): {{len(detections)}}")
if detections:
    print(f"      ✅ Found {{len(detections)}} detection(s)")
    print("      Note: Using 'hey_jarvis' model, not 'hey_nap'")
    print("      This shows the detection pipeline works!")
else:
    print(f"      ❌ No detections above threshold 0.5")
    print("      This is expected - 'Hey, Nap' won't match 'hey_jarvis' model")
    print("      The pipeline works, but you need a custom 'hey_nap' model")
    if max_score > 0.1:
        print(f"      However, max score {{max_score:.3f}} shows some similarity")
"""

# Run the test
try:
    result = subprocess.run(
        [sys.executable, '-c', test_script.format(wav_path=str(wav_path.absolute()))],
        capture_output=True,
        text=True,
        timeout=120
    )
    print(result.stdout)
    if result.stderr and "WARNING" not in result.stderr:
        print("Errors:", result.stderr)
except subprocess.TimeoutExpired:
    print("❌ Test timed out")
except FileNotFoundError:
    print("❌ Python not found")
except Exception as e:
    print(f"❌ Test failed: {e}")
    print("\nTrying alternative Python...")
    # Try with python3 explicitly
    try:
        result = subprocess.run(
            ['python3', '-c', test_script.format(wav_path=str(wav_path.absolute()))],
            capture_output=True,
            text=True,
            timeout=120
        )
        print(result.stdout)
        if result.stderr and "WARNING" not in result.stderr:
            print("Errors:", result.stderr)
    except Exception as e2:
        print(f"❌ Also failed: {e2}")
        print("\n💡 Try installing dependencies manually:")
        print("   pip3 install openwakeword numpy onnxruntime")
        print("   Then run this script again")

print("\n" + "="*60)
print("Summary:")
print("="*60)
print(f"✅ WAV file: {wav_path.absolute()}")
print("\nNext steps:")
print("1. If detection worked, the pipeline is functional")
print("2. For 'Hey, Nap', you need to train a custom model")
print("3. The ESP32 currently uses energy-based detection (not ML)")
print("="*60)
