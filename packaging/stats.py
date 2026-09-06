#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
"""Read-only adapters for the documented HiveOS and mmpOS custom interfaces."""
import json
import sys
import urllib.request


def convert(s, mode):
    devices = s['devices']
    if mode == 'mmpos':
        devices = sorted(devices, key=lambda d: d['label'] != 'CPU')
    bus = ['cpu' if d['label'] == 'CPU' else int(d['pci_bus'].split(':')[-2], 16)
           for d in devices]
    rates = [d['hashrate'] for d in devices]
    rejected = s['rejected'] + s['stale']
    if mode == 'hive':
        return {'hs': rates, 'hs_units': 'hs', 'temp': [d['temperature'] for d in devices],
                'fan': [d['fan'] for d in devices], 'uptime': int(s['uptime']),
                'ver': s['version'], 'ar': [s['accepted'], rejected], 'algo': 'blake2b',
                'bus_numbers': [None if b == 'cpu' else b for b in bus]}
    return {'busid': bus, 'hash': rates, 'units': 'hs',
            'air': [str(s['accepted']), '0', str(rejected)],
            'miner_name': 'supryolo', 'miner_version': s['version']}


if __name__ == '__main__':
    try:
        port = int(sys.argv[2])
        if not 1 <= port <= 65535:
            raise ValueError('port out of range')
        # Never send loopback telemetry through an environment-configured proxy.
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        with opener.open('http://127.0.0.1:%d/summary' % port, timeout=2) as response:
            snapshot = json.load(response)
        if snapshot['state'] == 'STOPPED':
            raise ValueError('miner stopped')
        print(json.dumps(convert(snapshot, sys.argv[1])))
    except Exception as exc:
        print('supryolo stats unavailable: ' + str(exc), file=sys.stderr)
        sys.exit(1)
