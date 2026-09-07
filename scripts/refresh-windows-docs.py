#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
"""Refresh release documentation while retaining the tested Windows executable."""
import pathlib
import sys
import zipfile

archive = pathlib.Path(sys.argv[1])
temporary = archive.with_suffix('.updated.zip')
root = pathlib.Path(__file__).resolve().parents[1]
with zipfile.ZipFile(archive) as original, zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as updated:
    for info in original.infolist():
        if info.filename == 'supryolo/README.md' or info.filename.startswith('supryolo/docs/'):
            continue
        updated.writestr(info, original.read(info.filename))
    updated.write(root / 'README.md', 'supryolo/README.md')
    for path in sorted((root / 'docs').rglob('*')):
        if path.is_file():
            updated.write(path, 'supryolo/' + path.relative_to(root).as_posix())
with zipfile.ZipFile(temporary) as check:
    if check.testzip() is not None:
        raise SystemExit('Updated archive failed integrity check')
    with zipfile.ZipFile(archive) as original:
        assert check.read('supryolo/supryolo.exe') == original.read('supryolo/supryolo.exe')
temporary.replace(archive)
