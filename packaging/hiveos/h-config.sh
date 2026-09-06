#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
# Sourced by HiveOS: pass configuration as data, never eval shell input.
CUSTOM_URL="${CUSTOM_URL:-}" CUSTOM_TEMPLATE="${CUSTOM_TEMPLATE:-}" \
CUSTOM_PASS="${CUSTOM_PASS:-x}" CUSTOM_USER_CONFIG="${CUSTOM_USER_CONFIG:-}" \
python3 - "${CUSTOM_CONFIG_FILENAME}" <<'PY'
import json, os, shlex, sys
urls = os.environ['CUSTOM_URL'].split()
if len(urls) != 1:
    raise SystemExit('supryolo requires exactly one pool URL')
url = urls[0]
if '://' not in url:
    url = 'stratum+tcp://' + url
user = os.environ['CUSTOM_TEMPLATE']
if not user or '%' in user:
    raise SystemExit('Set Wallet and worker template, e.g. %WAL%.%WORKER_NAME%')
args = ['--url', url, '--user', user, '--password', os.environ['CUSTOM_PASS'], '--no-cpu']
args += shlex.split(os.environ['CUSTOM_USER_CONFIG'])
# Fixed integration port; prevents stats accidentally polling another miner.
args += ['--api-port', '4068']
os.umask(0o077)
with open(sys.argv[1], 'w') as out:
    json.dump(args, out)
PY
