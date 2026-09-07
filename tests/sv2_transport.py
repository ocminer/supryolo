#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
"""Join two independent clients through pipes; no listening port or secrets."""
import os
import subprocess
import sys

left_read, left_write = os.pipe()
right_read, right_write = os.pipe()
a = b = None
try:
    a = subprocess.Popen([sys.argv[1]], stdin=left_read, stdout=right_write)
    b = subprocess.Popen([sys.argv[2]] + sys.argv[3:], stdin=right_read, stdout=left_write)
finally:
    for fd in (left_read, left_write, right_read, right_write):
        os.close(fd)
try:
    assert a.wait(timeout=30) == 0
    assert b.wait(timeout=30) == 0
finally:
    for process in (a, b):
        if process is not None and process.poll() is None:
            process.kill()
            process.wait()
