#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
import json
import os
import sys
with open(sys.argv[1]) as f:
    args = json.load(f)
os.execv('./supryolo', ['./supryolo'] + args + ['--log-file', sys.argv[2]])
