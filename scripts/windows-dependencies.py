#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
"""Bundle the recursive MinGW DLL import closure; Windows supplies system DLLs."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
exe = Path(sys.argv[1]).resolve()
prefix = Path(subprocess.check_output(['cygpath', '-w', '/ucrt64/bin'], text=True).strip())
lookup = {p.name.lower(): p for p in prefix.glob('*.dll')}
queue = [exe]
seen = set()
while queue:
    path = queue.pop()
    output = subprocess.check_output(['objdump', '-p', str(path)], text=True)
    for name in re.findall(r'DLL Name:\s*([^\r\n]+)', output):
        key = name.strip().lower()
        if key in seen or key not in lookup:
            continue
        seen.add(key)
        src = lookup[key]
        shutil.copy2(src, exe.parent / src.name)
        queue.append(src)
print('Bundled runtime DLLs:', ', '.join(sorted(seen)))
