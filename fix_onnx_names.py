#!/usr/bin/env python3
"""Fix ONNX model node names to avoid TensorFlow sanitization issues."""

import onnx
import sys

def fix_onnx_names(model_path, output_path):
    """Fix node names in ONNX model."""
    model = onnx.load(model_path)
    
    # Create a mapping of old names to new names
    name_map = {}
    
    # Fix all node names
    for node in model.graph.node:
        if node.name and '::' in node.name:
            old_name = node.name
            new_name = node.name.replace('::', '__')
            node.name = new_name
            name_map[old_name] = new_name
    
    # Fix all input references
    for node in model.graph.node:
        new_inputs = []
        for inp in node.input:
            if inp in name_map:
                new_inputs.append(name_map[inp])
            else:
                new_inputs.append(inp)
        node.input[:] = new_inputs
    
    # Fix all output references  
    for node in model.graph.node:
        new_outputs = []
        for out in node.output:
            if out in name_map:
                new_outputs.append(name_map[out])
            else:
                new_outputs.append(out)
        node.output[:] = new_outputs
    
    # Fix graph inputs/outputs
    for inp in model.graph.input:
        if inp.name in name_map:
            inp.name = name_map[inp.name]
    
    for out in model.graph.output:
        if out.name in name_map:
            out.name = name_map[out.name]
    
    onnx.save(model, output_path)
    print(f"✅ Fixed model saved to {output_path}")
    print(f"   Fixed {len(name_map)} node names")

if __name__ == "__main__":
    fix_onnx_names("hey_nap.onnx", "hey_nap_fixed.onnx")
