#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
set -euo pipefail
# Change ONLY this address to your BTCB2 payout address, then run ./start.sh.
WALLET="${WALLET:-YOUR_BTCB2_ADDRESS}"
WORKER="${WORKER:-rig1}"
PROTOCOL="${PROTOCOL:-sv1}"
case "$PROTOCOL" in
  sv1) DEFAULT_POOL=stratum+tcp://de.b2pool.io:4444 ;;
  sv2) DEFAULT_POOL=stratum2+tcp://de.b2pool.io:14444 ;;
  *) echo 'PROTOCOL must be sv1 or sv2' >&2; exit 2 ;;
esac
POOL="${POOL:-$DEFAULT_POOL}"
auth=()
if [[ "$POOL" == stratum2+tcp://* ]]; then
  auth=(--sv2-authority "${SV2_AUTHORITY:-cc22ab3495b26c1a5d0a5c834df4ae8926cc7dbf8ef6c292f951f174280cd323}")
fi
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
  cpu) devices=(--no-gpu); POOL="${POOL/:4444/:5555}"; POOL="${POOL/:14444/:15555}" ;;
  mixed) devices=() ;;
  *) echo 'MODE must be gpu, cpu or mixed' >&2; exit 2 ;;
esac
BIN="$HERE/supryolo"
[[ -x "$BIN" ]] || BIN="$HERE/build/supryolo"
exec "$BIN" --url "$POOL" --user "$WALLET.$WORKER" "${devices[@]}" "${auth[@]}" "$@"
