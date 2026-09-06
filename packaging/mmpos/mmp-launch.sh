#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
exec python3 ./mmp-launch.py "$@"
