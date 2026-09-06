#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
import argparse
import os
import sys
p = argparse.ArgumentParser()
p.add_argument('--coin', default='BTCB2')
p.add_argument('--pool', required=True)
p.add_argument('--pool-protocol', default='tcp', choices=['tcp', 'tls', 'ssl'])
p.add_argument('--user', required=True)
p.add_argument('--password', default='x')
p.add_argument('--api-port', type=int, default=4068)
a, extra = p.parse_known_args()
if a.coin.lower() not in ('btcb2', 'b2', 'blake2b'):
    p.error('Only BTCB2 BLAKE2b is supported; select the correct coin')
if a.api_port != 4068:
    p.error('Custom integration uses API port 4068; set it in the profile')
url = a.pool if '://' in a.pool else 'stratum+' + a.pool_protocol + '://' + a.pool
args = ['./supryolo', '--url', url, '--user', a.user, '--password', a.password]
if '--no-gpu' not in extra and '--cpu' not in extra:
    args.append('--no-cpu')
os.execv(args[0], args + extra + ['--api-port', '4068'])
