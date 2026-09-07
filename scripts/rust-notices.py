#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
"""Collect original notices for the locked Rust dependencies; no build paths."""
import json
import pathlib
import shutil
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
metadata = json.loads(subprocess.check_output([
    'cargo', 'metadata', '--locked', '--format-version', '1',
    '--manifest-path', str(root / 'rust/sv2-noise/Cargo.toml')]))
output = root / 'third_party/rust-dependencies'
output.mkdir(parents=True, exist_ok=True)
index = []
for package in metadata['packages']:
    if package['name'] == 'supryolo-sv2-noise':
        continue
    folder = pathlib.Path(package['manifest_path']).parent
    dest = output / (package['name'] + '-' + package['version'])
    copied = []
    for path in folder.rglob('*'):
        if path.is_file() and path.name.upper().startswith(('LICENSE', 'COPYING', 'COPYRIGHT', 'NOTICE')):
            relative = path.relative_to(folder)
            target = dest / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
            copied.append(str(relative))
    if not copied and package['license'] == 'CC0-1.0':
        dest.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / 'third_party/license-text/CC0-1.0.txt', dest / 'CC0-1.0.txt')
        copied.append('CC0-1.0.txt')
    if not copied and 'stratum-mining/stratum' in (package['source'] or ''):
        dest.mkdir(parents=True, exist_ok=True)
        for name in ['LICENSE-MIT', 'LICENSE-APACHE']:
            shutil.copyfile(root / 'third_party/sri' / name, dest / name)
            copied.append(name)
    if not copied:
        raise SystemExit('No original license file found: ' + package['name'])
    index.append(dict(name=package['name'], version=package['version'],
                      license=package['license'], repository=package['repository'], files=sorted(copied)))
(output / 'index.json').write_text(json.dumps(index, indent=2) + '\n')
