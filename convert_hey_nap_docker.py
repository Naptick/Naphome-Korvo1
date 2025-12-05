#!/usr/bin/env python3
"""Docker version - converts ONNX to TFLite with runtime handler patching."""

import sys
import os
import types

# Create tensorflow-addons stub
print("Creating tensorflow-addons stub...")
tfa = types.ModuleType('tensorflow_addons')
tfa.seq2seq = types.ModuleType('seq2seq')
tfa.seq2seq.hardmax = lambda x: x
tfa.text = types.ModuleType('text')
tfa.image = types.ModuleType('image')
sys.modules['tensorflow_addons'] = tfa
print("✅ tensorflow-addons stub created")

import onnx
import tensorflow as tf
from onnx_tf.backend import prepare
import tempfile

# Patch the flatten handler directly
import onnx_tf.handlers.backend.flatten as flatten_module

original_common = flatten_module.Flatten._common

@classmethod
def patched_common(cls, node, **kwargs):
    """Patched _common that handles name sanitization."""
    tensor_dict = kwargs["tensor_dict"]
    input_name = node.inputs[0]
    
    # Try original name
    if input_name in tensor_dict:
        x = tensor_dict[input_name]
    else:
        # Try sanitized version
        sanitized = input_name.replace('::', '__')
        if sanitized in tensor_dict:
            x = tensor_dict[sanitized]
        else:
            # Last resort: find any key containing the name
            for key in list(tensor_dict.keys()):
                if input_name.split('::')[-1] in key or sanitized.split('__')[-1] in key:
                    x = tensor_dict[key]
                    break
            else:
                # Print available keys for debugging
                available = list(tensor_dict.keys())[:10]
                raise KeyError(f"Tensor '{input_name}' not found. Available: {available}")
    
    axis = node.attrs.get("axis", 1)
    # Call original with fixed x - need to reconstruct the call
    # The original does: return cls.make_node_from_tf_op(tf.keras.layers.Flatten(), [x])
    # But we need to use the class method properly
    return original_common.__func__(cls, node, **{**kwargs, 'tensor_dict': {**tensor_dict, input_name: x}})

flatten_module.Flatten._common = patched_common
print("✅ Patched Flatten handler")

onnx_file = "hey_nap.onnx"
tflite_file = "hey_nap.tflite"

print(f"\n🔄 Converting {onnx_file} to TFLite...")

print("\nStep 1: Loading ONNX model...")
onnx_model = onnx.load(onnx_file)
print(f"   ✅ ONNX model loaded (ONNX version: {onnx.__version__})")

print("\nStep 2: Converting to TensorFlow...")
try:
    tf_rep = prepare(onnx_model, strict=False)
    print("   ✅ TensorFlow representation created")
except Exception as e:
    print(f"   ❌ Failed: {e}")
    raise

print("\nStep 3: Exporting to SavedModel...")
with tempfile.TemporaryDirectory() as tmp_dir:
    tf_model_path = os.path.join(tmp_dir, "saved_model")
    tf_rep.export_graph(tf_model_path)
    print("   ✅ SavedModel exported")
    
    print("\nStep 4: Converting to TFLite...")
    converter = tf.lite.TFLiteConverter.from_saved_model(tf_model_path)
    tflite_model = converter.convert()
    
    print("\nStep 5: Saving TFLite file...")
    with open(tflite_file, 'wb') as f:
        f.write(tflite_model)

file_size = os.path.getsize(tflite_file) / 1024
print(f"\n✅ Conversion successful!")
print(f"   File: {tflite_file}")
print(f"   Size: {file_size:.1f} KB")
