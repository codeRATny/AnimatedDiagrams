#!/usr/bin/env python3
"""Reject ELF files requiring a newer glibc than the portable Linux baseline."""
import pathlib
import re
import subprocess
import sys

baseline = (2, 28)
failed = False
root = pathlib.Path(sys.argv[1])
if not root.is_dir():
    sys.exit(f'Not a directory: {root}')
count = 0
for path in root.rglob('*'):
    if not path.is_file() or path.is_symlink():
        continue
    with path.open('rb') as stream:
        if stream.read(4) != b'\x7fELF':
            continue
    count += 1
    result = subprocess.run(['readelf', '--version-info', str(path)], capture_output=True, text=True, check=True)
    needs = result.stdout.partition('Version needs section')[2]
    for version in re.findall(r'Name: GLIBC_(\d+(?:\.\d+)+)', needs):
        if tuple(map(int, version.split('.'))) > baseline:
            print(f'{path}: requires GLIBC_{version}', file=sys.stderr)
            failed = True
if failed:
    sys.exit(1)
if not count:
    sys.exit('No ELF files found.')
print(f'Checked {count} ELF files: glibc 2.28 or older required.')
