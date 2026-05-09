import os
from pathlib import Path

def prepend_copyright():
    header = (
        "/*\n"
        " * Copyright (c) 2026 Demizo\n"
        " *\n"
        " * SPDX-License-Identifier: Apache-2.0\n"
        " */\n\n"
    )
    
    targets = ['lib', 'include', 'samples', 'tests']
    extensions = ['*.c', '*.h']

    for target_dir in targets:
        path = Path(target_dir)
        if not path.exists():
            print(f"Directory '{target_dir}' not found, skipping...")
            continue

        for ext in extensions:
            for file_path in path.rglob(ext):
                if any(part.startswith('build') for part in file_path.parts):
                    continue
                try:
                    with open(file_path, 'r', encoding='utf-8') as f:
                        content = f.read()

                    # Check if the header is already there to avoid duplicates
                    if not content.startswith(header):
                        print(f"Adding header to: {file_path}")
                        with open(file_path, 'w', encoding='utf-8') as f:
                            f.write(header + content)

                except Exception as e:
                    print(f"Error processing {file_path}: {e}")

if __name__ == "__main__":
    prepend_copyright()