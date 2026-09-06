#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
set -euo pipefail
BIN="${1:?binary required}"
OUT="${2:?output directory required}"
FLAVOR="${3:-linux-x86_64}"
VERSION=0.1.0
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/supryolo/third_party/nlohmann"
cp "$BIN" "$STAGE/supryolo/supryolo"
strip "$STAGE/supryolo/supryolo"
cp start.sh LICENSE THIRD_PARTY.md README.md "$STAGE/supryolo/"
cp third_party/nlohmann/LICENSE.MIT "$STAGE/supryolo/third_party/nlohmann/"
cp -r docs "$STAGE/supryolo/"
if [[ -n "${RUNTIME_LICENSE_DIR:-}" ]]; then
  cp -r "$RUNTIME_LICENSE_DIR" "$STAGE/supryolo/third_party/runtime"
else
  echo "RUNTIME_LICENSE_DIR is required for bundled runtime notices" >&2; exit 2
fi
if [[ "$FLAVOR" == hiveos || "$FLAVOR" == mmpos ]]; then
  cp packaging/"$FLAVOR"/* "$STAGE/supryolo/"
  cp packaging/stats.py "$STAGE/supryolo/"
fi
if [[ "$FLAVOR" == hiveos ]]; then
  ARCHIVE="supryolo-${VERSION}.hiveos.tar.gz"
else
  ARCHIVE="supryolo-${VERSION}-${FLAVOR}.tar.gz"
fi
tar --sort=name --owner=0 --group=0 --numeric-owner -czf "$OUT/$ARCHIVE" -C "$STAGE" supryolo
printf '%s\n' "$OUT/$ARCHIVE"
