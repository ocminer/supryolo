#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
set -euo pipefail
# Change ONLY this address to your BTCB2 payout address, then run ./start.sh.
WALLET="${WALLET:-YOUR_BTCB2_ADDRESS}"
WORKER="${WORKER:-rig1}"
POOL="${POOL:-stratum+tcp://de.b2pool.io:4444}"
# Defaults: all GPUs, CPU disabled. Example: ./start.sh --gpu-device 0,2
# CPU only: MODE=cpu ./start.sh --cpu-threads 4
MODE="${MODE:-gpu}"
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ -z "$WALLET" || "$WALLET" == YOUR_BTCB2_ADDRESS ]]; then
  echo 'Edit WALLET in start.sh and replace YOUR_BTCB2_ADDRESS with your BTCB2 address.' >&2
  exit 2
fi
case "$MODE" in
  gpu) devices=(--no-cpu) ;;
  cpu) devices=(--no-gpu); POOL="${POOL/:4444/:5555}" ;;
  mixed) devices=() ;;
  *) echo 'MODE must be gpu, cpu or mixed' >&2; exit 2 ;;
esac
BIN="$HERE/supryolo"
[[ -x "$BIN" ]] || BIN="$HERE/build/supryolo"
exec "$BIN" --url "$POOL" --user "$WALLET.$WORKER" "${devices[@]}" "$@"
