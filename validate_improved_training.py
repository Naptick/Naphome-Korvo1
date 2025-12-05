#!/usr/bin/env python3
"""
Validate the improved training setup locally.

This script checks:
- Config file is valid
- Training script exists
- Dependencies are available
- Can import required modules
"""

import os
import sys
import yaml
import subprocess
from pathlib import Path

def check_config(config_path):
    """Validate the training config file."""
    print("📋 Validating config file...")
    
    if not os.path.exists(config_path):
        print(f"   ❌ Config file not found: {config_path}")
        return False
    
    try:
        with open(config_path, 'r') as f:
            config = yaml.safe_load(f)
        
        # Required fields
        required_fields = [
            'target_phrase', 'model_name', 'model_type',
            'layer_size', 'n_blocks', 'n_samples', 'n_samples_val',
            'output_dir', 'steps'
        ]
        
        missing = []
        for field in required_fields:
            if field not in config:
                missing.append(field)
        
        if missing:
            print(f"   ❌ Missing required fields: {', '.join(missing)}")
            return False
        
        # Check improved values
        print(f"   ✅ Config file valid")
        print(f"      Model: {config['layer_size']} units, {config['n_blocks']} blocks")
        print(f"      Training samples: {config['n_samples']:,}")
        print(f"      Training steps: {config['steps']:,}")
        print(f"      Augmentation rounds: {config.get('augmentation_rounds', 1)}")
        
        if config['layer_size'] >= 256 and config['n_blocks'] >= 2:
            print(f"   ✅ Improved model size confirmed")
        else:
            print(f"   ⚠️  Model size not improved (layer_size={config['layer_size']}, n_blocks={config['n_blocks']})")
        
        if config['n_samples'] >= 200000:
            print(f"   ✅ Increased training data confirmed")
        else:
            print(f"   ⚠️  Training data not increased (n_samples={config['n_samples']})")
        
        return True
        
    except yaml.YAMLError as e:
        print(f"   ❌ Invalid YAML: {e}")
        return False
    except Exception as e:
        print(f"   ❌ Error reading config: {e}")
        return False

def check_training_script():
    """Check if training script exists."""
    print("\n📜 Checking training script...")
    
    script_path = Path(__file__).parent / "components" / "openwakeword" / "lib" / "openwakeword" / "train.py"
    
    if not script_path.exists():
        print(f"   ❌ Training script not found: {script_path}")
        return False
    
    print(f"   ✅ Training script found: {script_path}")
    return True

def check_dependencies():
    """Check if required dependencies are installed."""
    print("\n📦 Checking dependencies...")
    
    dependencies = {
        'torch': 'PyTorch',
        'numpy': 'NumPy',
        'scipy': 'SciPy',
        'sklearn': 'scikit-learn',
        'yaml': 'PyYAML',
        'openwakeword': 'OpenWakeWord'
    }
    
    missing = []
    for module, name in dependencies.items():
        try:
            __import__(module)
            print(f"   ✅ {name}")
        except ImportError:
            print(f"   ❌ {name} not installed")
            missing.append(name)
    
    if missing:
        print(f"\n   ⚠️  Missing: {', '.join(missing)}")
        print(f"   Install with: pip install {' '.join(missing)}")
        return False
    
    return True

def check_colab_notebook():
    """Check if Colab notebook exists and is valid."""
    print("\n📓 Checking Colab notebook...")
    
    notebook_path = Path(__file__).parent / "train_hey_nap_improved_colab.ipynb"
    
    if not notebook_path.exists():
        print(f"   ❌ Notebook not found: {notebook_path}")
        return False
    
    try:
        import json
        with open(notebook_path, 'r') as f:
            notebook = json.load(f)
        
        if 'cells' not in notebook:
            print(f"   ❌ Invalid notebook format")
            return False
        
        print(f"   ✅ Notebook found with {len(notebook['cells'])} cells")
        
        # Check if it uses subprocess (correct approach)
        notebook_content = json.dumps(notebook)
        if 'subprocess' in notebook_content or '!python' in notebook_content:
            print(f"   ✅ Uses correct training approach")
        else:
            print(f"   ⚠️  May need to use subprocess for training")
        
        return True
        
    except Exception as e:
        print(f"   ❌ Error reading notebook: {e}")
        return False

def test_config_loading():
    """Test loading the config in Python."""
    print("\n🧪 Testing config loading...")
    
    config_path = Path(__file__).parent / "training_config_hey_nap_improved.yaml"
    
    try:
        with open(config_path, 'r') as f:
            config = yaml.safe_load(f)
        
        print(f"   ✅ Config loads successfully")
        print(f"      Target phrase: {config['target_phrase']}")
        print(f"      Model name: {config['model_name']}")
        print(f"      Output dir: {config['output_dir']}")
        
        return True
    except Exception as e:
        print(f"   ❌ Error loading config: {e}")
        return False

def main():
    print("="*70)
    print("Improved Training Setup Validation")
    print("="*70)
    print()
    
    config_path = Path(__file__).parent / "training_config_hey_nap_improved.yaml"
    
    results = []
    
    # Run checks
    results.append(("Config file", check_config(str(config_path))))
    results.append(("Training script", check_training_script()))
    results.append(("Dependencies", check_dependencies()))
    results.append(("Colab notebook", check_colab_notebook()))
    results.append(("Config loading", test_config_loading()))
    
    # Summary
    print("\n" + "="*70)
    print("Validation Summary")
    print("="*70)
    
    all_passed = True
    for name, passed in results:
        status = "✅ PASS" if passed else "❌ FAIL"
        print(f"  {status} - {name}")
        if not passed:
            all_passed = False
    
    print()
    
    if all_passed:
        print("✅ All checks passed! Ready for training.")
        print()
        print("Next steps:")
        print("  1. Upload train_hey_nap_improved_colab.ipynb to Colab")
        print("  2. Run all cells")
        print("  3. Download the improved ONNX model")
        print("  4. Convert with: ./convert_with_docker.sh")
        return 0
    else:
        print("❌ Some checks failed. Please fix the issues above.")
        return 1

if __name__ == "__main__":
    sys.exit(main())
