#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
    p = Path(d)
    for src in [root/'start.sh', root/'packaging/mmpos/mmp-launch.py']:
        shutil.copy(src, p/src.name)
    (p/'supryolo').write_text('#!/usr/bin/env python3\nimport json,sys\nprint(json.dumps(sys.argv[1:]))\n')
    (p/'supryolo').chmod(0o755)
    env = os.environ.copy()
    env.pop('WALLET', None)
    r = subprocess.run(['bash', str(p/'start.sh')], env=env, capture_output=True)
    assert r.returncode == 2
    env.update(WALLET='test-address', WORKER='test-rig')
    r = subprocess.run(['bash', str(p/'start.sh'), '--gpu-device', '1,0'], env=env, capture_output=True, text=True, check=True)
    a = json.loads(r.stdout)
    assert a[-2:] == ['--gpu-device','1,0'] and '--no-cpu' in a
    env['MODE']='cpu'
    r = subprocess.run(['bash', str(p/'start.sh')], env=env, capture_output=True, text=True, check=True)
    a = json.loads(r.stdout)
    assert '--no-gpu' in a and 'stratum+tcp://de.b2pool.io:5555' in a
    env.update(CUSTOM_URL='de.b2pool.io:4444', CUSTOM_TEMPLATE='test.worker', CUSTOM_PASS='x',
               CUSTOM_USER_CONFIG='--gpu-device 1,0 --log-file "literal $(touch bad)"',
               CUSTOM_CONFIG_FILENAME=str(p/'flight.json'))
    subprocess.run(['bash', str(root/'packaging/hiveos/h-config.sh')], env=env, check=True)
    a=json.loads((p/'flight.json').read_text())
    assert 'literal $(touch bad)' in a and a[-2:]==['--api-port','4068']
    assert (p/'flight.json').stat().st_mode & 0o077 == 0
    r=subprocess.run(['python3','mmp-launch.py','--coin','BTCB2','--pool','de.b2pool.io:5555',
                      '--user','test.worker','--no-gpu','--cpu-threads','2'],cwd=p,text=True,capture_output=True,check=True)
    a=json.loads(r.stdout)
    assert '--no-cpu' not in a and '--no-gpu' in a and a[-2:]==['--api-port','4068']
print('Launcher address guard, CPU/GPU choices, Hive quoting/permissions and mmpOS forwarding passed')
