#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
# Sourced callback: publish the two variables expected by the Hive agent.
khs=0
stats=null
if stats=$(python3 "$(dirname -- "${BASH_SOURCE[0]}")/stats.py" hive 4068); then
  khs=$(python3 -c 'import json,sys; print(sum(json.load(sys.stdin)["hs"])/1000)' <<< "$stats")
else
  stats=null
fi
