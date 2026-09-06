#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
source ./h-manifest.conf
mkdir -p -- "$(dirname -- "$CUSTOM_LOG_BASENAME")"
exec python3 ./h-launch.py "$CUSTOM_CONFIG_FILENAME" "$CUSTOM_LOG_BASENAME.log"
