#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
import importlib.util
import json
import pathlib
import socket
import subprocess
import sys
import time
import urllib.request

root = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('adapter', root / 'packaging/stats.py')
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)
with socket.socket() as s:
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
p = subprocess.Popen([sys.argv[1], str(port)])
try:
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    for _ in range(40):
        try:
            with opener.open('http://127.0.0.1:%d/summary' % port, timeout=2) as r:
                v = json.load(r)
            break
        except OSError:
            time.sleep(.05)
    else:
        raise AssertionError('API never ready')
    assert v['accepted'] == 7 and v['pending'] == 4 and v['stale'] == 2
    assert 'worker' not in v and 'pool' not in v
    h = adapter.convert(v, 'hive')
    assert h['hs'] == [2000000, 1000000] and h['bus_numbers'] == [10, None]
    assert h['ar'] == [7, 3] and h['temp'] == [62, None]
    m = adapter.convert(v, 'mmpos')
    assert m['busid'] == ['cpu', 10] and m['hash'] == [1000000, 2000000]
    assert m['air'] == ['7', '0', '3']
    for request in (b'POST /summary HTTP/1.1\r\nHost: localhost\r\n\r\n',
                    b'GET /control HTTP/1.1\r\nHost: localhost\r\n\r\n'):
        with socket.create_connection(('127.0.0.1', port), timeout=3) as s:
            s.sendall(request)
            assert s.recv(4096).startswith(b'HTTP/1.1 404')
    # Slow/incomplete clients must time out so subsequent monitoring still works.
    with socket.create_connection(('127.0.0.1', port), timeout=3) as slow:
        slow.sendall(b'GET /')
        time.sleep(1.2)
        assert slow.recv(4096).startswith(b'HTTP/1.1 404')
    with opener.open('http://127.0.0.1:%d/summary' % port, timeout=3) as r:
        assert json.load(r)['accepted'] == 7
    print('API HTTP bounds, read-only routes and Hive/MMPOS device/unit/share mapping passed')
finally:
    p.terminate()
    p.wait(timeout=5)
