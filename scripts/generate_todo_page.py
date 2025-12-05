#!/usr/bin/env python3
"""
Generate TODO list HTML page from codebase comments.
Extracts TODO, FIXME, XXX, HACK, NOTE comments and generates a categorized HTML page.
"""

import os
import re
import sys
from pathlib import Path
from datetime import datetime
from collections import defaultdict

# File extensions to search
CODE_EXTENSIONS = {'.c', '.cpp', '.h', '.hpp', '.py', '.js', '.ts', '.md', '.yaml', '.yml'}

# Patterns to match TODO comments
TODO_PATTERNS = [
    (r'//\s*(TODO|FIXME|XXX|HACK|NOTE|BUG):\s*(.+)', 'single_line'),
    (r'#\s*(TODO|FIXME|XXX|HACK|NOTE|BUG):\s*(.+)', 'single_line'),
    (r'/\*\s*(TODO|FIXME|XXX|HACK|NOTE|BUG):\s*(.+?)\s*\*/', 'multi_line'),
]

# Directories to exclude
EXCLUDE_DIRS = {
    '.git', 'build', 'deps', 'dependencies', '__pycache__', 
    'node_modules', '.pio', '.vscode', '.idea', 'components/openwakeword/lib'
}

# Files to exclude
EXCLUDE_FILES = {
    'phase-0.9-spec.html',  # Already has structured content
}

class TodoItem:
    def __init__(self, file_path, line_num, todo_type, message, context=''):
        self.file_path = file_path
        self.line_num = line_num
        self.todo_type = todo_type.upper()
        self.message = message.strip()
        self.context = context.strip()
        # Categorize based on content
        self.category = self._categorize()
    
    def _categorize(self):
        """Categorize TODO based on content"""
        msg_lower = self.message.lower()
        if any(word in msg_lower for word in ['sensor', 'i2c', 'spi', 'gpio', 'hardware']):
            return 'Hardware'
        elif any(word in msg_lower for word in ['audio', 'i2s', 'mic', 'speaker', 'wake word', 'tts', 'stt']):
            return 'Audio'
        elif any(word in msg_lower for word in ['wifi', 'mqtt', 'cloud', 'aws', 'iot', 'http', 'tls', 'certificate']):
            return 'Networking'
        elif any(word in msg_lower for word in ['gemini', 'llm', 'api', 'voice assistant']):
            return 'AI/Voice'
        elif any(word in msg_lower for word in ['led', 'display', 'ui', 'web', 'dashboard']):
            return 'UI/Display'
        elif any(word in msg_lower for word in ['memory', 'ram', 'psram', 'heap', 'buffer']):
            return 'Memory'
        elif any(word in msg_lower for word in ['test', 'testing', 'validation', 'demo']):
            return 'Testing'
        elif any(word in msg_lower for word in ['error', 'bug', 'fix', 'issue']):
            return 'Bugs/Fixes'
        else:
            return 'General'

def extract_todos_from_file(file_path):
    """Extract TODO items from a single file"""
    todos = []
    
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
            
        for line_num, line in enumerate(lines, 1):
            # Try each pattern
            for pattern, pattern_type in TODO_PATTERNS:
                match = re.search(pattern, line, re.IGNORECASE)
                if match:
                    todo_type = match.group(1)
                    message = match.group(2)
                    # Get context (previous and next line if available)
                    context_lines = []
                    if line_num > 1:
                        context_lines.append(lines[line_num - 2].strip())
                    context_lines.append(line.strip())
                    if line_num < len(lines):
                        context_lines.append(lines[line_num].strip())
                    context = ' | '.join(context_lines)
                    
                    todos.append(TodoItem(
                        str(file_path),
                        line_num,
                        todo_type,
                        message,
                        context
                    ))
                    break
    except Exception as e:
        print(f"Error reading {file_path}: {e}", file=sys.stderr)
    
    return todos

def scan_codebase(root_dir):
    """Scan codebase for TODO items"""
    todos = []
    root_path = Path(root_dir)
    
    for file_path in root_path.rglob('*'):
        # Skip excluded directories
        if any(excluded in file_path.parts for excluded in EXCLUDE_DIRS):
            continue
        
        # Skip excluded files
        if file_path.name in EXCLUDE_FILES:
            continue
        
        # Only process code files
        if file_path.suffix in CODE_EXTENSIONS and file_path.is_file():
            file_todos = extract_todos_from_file(file_path)
            todos.extend(file_todos)
    
    return todos

def generate_html(todos, output_path):
    """Generate HTML page from TODO items"""
    
    # Group by category
    by_category = defaultdict(list)
    by_type = defaultdict(list)
    
    for todo in todos:
        by_category[todo.category].append(todo)
        by_type[todo.todo_type].append(todo)
    
    # Sort categories
    category_order = ['Hardware', 'Audio', 'Networking', 'AI/Voice', 'UI/Display', 
                     'Memory', 'Testing', 'Bugs/Fixes', 'General']
    categories = sorted(by_category.keys(), key=lambda x: category_order.index(x) if x in category_order else 999)
    
    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Naphome-Korvo1 TODO List</title>
    <style>
        * {{
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }}
        
        body {{
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: #333;
            line-height: 1.6;
            min-height: 100vh;
            padding: 20px;
        }}
        
        .container {{
            max-width: 1200px;
            margin: 0 auto;
            background: white;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            overflow: hidden;
        }}
        
        header {{
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 40px;
            text-align: center;
        }}
        
        header h1 {{
            font-size: 2.5em;
            margin-bottom: 10px;
        }}
        
        header .meta {{
            font-size: 0.9em;
            opacity: 0.9;
            margin-top: 20px;
        }}
        
        .content {{
            padding: 40px;
        }}
        
        .stats {{
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin: 30px 0;
        }}
        
        .stat-card {{
            background: #f8f9fa;
            padding: 20px;
            border-radius: 10px;
            text-align: center;
            border-left: 4px solid #667eea;
        }}
        
        .stat-card .number {{
            font-size: 2.5em;
            font-weight: bold;
            color: #667eea;
        }}
        
        .stat-card .label {{
            color: #666;
            margin-top: 5px;
        }}
        
        .section {{
            margin-bottom: 40px;
        }}
        
        .section h2 {{
            color: #667eea;
            font-size: 1.8em;
            margin-bottom: 20px;
            border-bottom: 3px solid #667eea;
            padding-bottom: 10px;
        }}
        
        .todo-item {{
            background: #f8f9fa;
            padding: 15px;
            margin-bottom: 15px;
            border-radius: 8px;
            border-left: 4px solid #667eea;
        }}
        
        .todo-item.todo {{
            border-left-color: #f57c00;
        }}
        
        .todo-item.fixme {{
            border-left-color: #d32f2f;
        }}
        
        .todo-item.note {{
            border-left-color: #1976d2;
        }}
        
        .todo-item.hack {{
            border-left-color: #7b1fa2;
        }}
        
        .todo-header {{
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 10px;
        }}
        
        .todo-type {{
            display: inline-block;
            padding: 4px 12px;
            border-radius: 12px;
            font-size: 0.85em;
            font-weight: 600;
            text-transform: uppercase;
        }}
        
        .todo-type.TODO {{
            background: #fff3e0;
            color: #f57c00;
        }}
        
        .todo-type.FIXME {{
            background: #ffebee;
            color: #d32f2f;
        }}
        
        .todo-type.NOTE {{
            background: #e3f2fd;
            color: #1976d2;
        }}
        
        .todo-type.HACK {{
            background: #f3e5f5;
            color: #7b1fa2;
        }}
        
        .todo-file {{
            font-family: 'Courier New', monospace;
            font-size: 0.9em;
            color: #666;
        }}
        
        .todo-message {{
            color: #333;
            font-weight: 500;
            margin: 10px 0;
        }}
        
        .todo-context {{
            font-family: 'Courier New', monospace;
            font-size: 0.85em;
            color: #999;
            background: #fff;
            padding: 8px;
            border-radius: 4px;
            margin-top: 8px;
        }}
        
        .back-link {{
            display: inline-block;
            margin-bottom: 20px;
            color: #667eea;
            text-decoration: none;
            font-weight: 600;
        }}
        
        .back-link:hover {{
            text-decoration: underline;
        }}
        
        .empty-state {{
            text-align: center;
            padding: 40px;
            color: #999;
        }}
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>📋 Naphome-Korvo1 TODO List</h1>
            <div class="meta">
                <p>Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</p>
                <p>Total Items: {len(todos)}</p>
            </div>
        </header>
        
        <div class="content">
            <a href="features.html" class="back-link">← Back to Features Page</a>
            
            <div class="stats">
                <div class="stat-card">
                    <div class="number">{len(todos)}</div>
                    <div class="label">Total Items</div>
                </div>
                <div class="stat-card">
                    <div class="number">{len(by_type.get('TODO', []))}</div>
                    <div class="label">TODO</div>
                </div>
                <div class="stat-card">
                    <div class="number">{len(by_type.get('FIXME', []))}</div>
                    <div class="label">FIXME</div>
                </div>
                <div class="stat-card">
                    <div class="number">{len(by_type.get('NOTE', []))}</div>
                    <div class="label">NOTE</div>
                </div>
            </div>
"""
    
    # Generate sections by category
    for category in categories:
        category_todos = by_category[category]
        html += f"""
            <div class="section">
                <h2>{category} ({len(category_todos)})</h2>
"""
        
        if not category_todos:
            html += '<div class="empty-state">No items in this category</div>'
        else:
            for todo in sorted(category_todos, key=lambda x: (x.todo_type, x.file_path)):
                html += f"""
                <div class="todo-item {todo.todo_type.lower()}">
                    <div class="todo-header">
                        <span class="todo-type {todo.todo_type}">{todo.todo_type}</span>
                        <span class="todo-file">{todo.file_path}:{todo.line_num}</span>
                    </div>
                    <div class="todo-message">{todo.message}</div>
                    {f'<div class="todo-context">{todo.context}</div>' if todo.context else ''}
                </div>
"""
        
        html += "            </div>\n"
    
    html += """
        </div>
    </div>
</body>
</html>
"""
    
    # Write to file
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(html)
    
    print(f"Generated TODO page: {output_path}")
    print(f"Total TODOs found: {len(todos)}")
    for category in categories:
        print(f"  {category}: {len(by_category[category])}")

if __name__ == '__main__':
    # Get root directory (project root)
    root_dir = Path(__file__).parent.parent
    
    # Output path
    output_path = root_dir / 'docs' / 'todo.html'
    
    # Scan for TODOs
    print("Scanning codebase for TODO items...")
    todos = scan_codebase(root_dir)
    
    # Generate HTML
    generate_html(todos, output_path)
    
    sys.exit(0 if todos else 1)
