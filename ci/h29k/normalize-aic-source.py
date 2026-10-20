#!/usr/bin/env python3
"""Normalize CRLF only in text files targeted by the pinned AIC patches."""
import pathlib
import sys

root = pathlib.Path(sys.argv[1]).resolve(strict=True)
patch_dir = pathlib.Path(sys.argv[2])
targets = set()
for patch in sorted(patch_dir.glob('*.patch')):
    for line in patch.read_text(encoding='utf-8').splitlines():
        if line.startswith('+++ b/'):
            relative = pathlib.PurePosixPath(line[6:].split('\t', 1)[0])
            if relative.is_absolute() or '..' in relative.parts:
                raise SystemExit(f'Unsafe patch target: {relative}')
            targets.add(relative)
if not targets:
    raise SystemExit('No AIC patch targets found')
for relative in sorted(targets):
    path = root.joinpath(*relative.parts)
    resolved = path.resolve(strict=True)
    if not resolved.is_relative_to(root) or path.is_symlink():
        raise SystemExit(f'Unsafe source file: {relative}')
    data = path.read_bytes()
    if b'\0' in data:
        raise SystemExit(f'Binary patch target: {relative}')
    if b'\r\n' in data:
        path.write_bytes(data.replace(b'\r\n', b'\n'))
        print(f'AIC source CRLF normalized: {relative}')
