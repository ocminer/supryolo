#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Local protocol integration test. No external pool or wallet is used."""
import hashlib,json,socket,subprocess,sys,threading,time
from fractions import Fraction

binary=sys.argv[1]
cpu='--cpu' in sys.argv[2:]
mixed='--mixed' in sys.argv[2:]
prefixes=set()
listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(10)
errors=[];accepted=0;stale=0;epochs=0
coin='0000001b11a8e2c16084cc487838df37dd867fed260104a09be4298db7a1e833ff4b0900000000'
prev='000000000000'+'12'*26;ntime='00000000c0ffee00'

def serve():
 global accepted,stale,epochs
 try:
  conn,_=listener.accept();conn.settimeout(.05)
  buf=b'';en='01020304';seen=set();target=0;started=time.monotonic();updated=False;subscribed=False;ready=False
  def send(j):conn.sendall((json.dumps(j)+'\n').encode())
  def notify(clean):send({'id':None,'method':'mining.notify','params':['same-id',prev,coin,'',[],'20000000','190f0b50',ntime,clean]})
  while time.monotonic()-started<6:
   if ready and not updated and time.monotonic()-started>1.5:
    en='05060708';send({'id':None,'method':'mining.set_extranonce','params':[en,8]});notify(True);updated=True;epochs+=1
   try:data=conn.recv(65536)
   except socket.timeout:continue
   if not data:break
   buf+=data
   while b'\n' in buf:
    line,buf=buf.split(b'\n',1);q=json.loads(line);method=q['method'];p=q['params']
    if method=='mining.subscribe':
     assert p==['supryolo/0.1.0-dev'];send({'id':q['id'],'result':[[],en,8],'error':None});subscribed=True
    elif method=='mining.extranonce.subscribe':send({'id':q['id'],'result':True,'error':None})
    elif method=='mining.authorize':
     assert subscribed;send({'id':q['id'],'result':True,'error':None})
     diff=.00001 if cpu else .01;target=int(Fraction(65535*(1<<208))/Fraction(str(diff)))
     send({'id':None,'method':'mining.set_difficulty','params':[diff]});notify(True);ready=True
    elif method=='mining.submit':
     assert len(p)==5 and p[0]=='ocminer-test' and p[1]=='same-id'
     assert all(len(p[i])==16 for i in [2,3,4]);key=(en,*p[1:]);assert key not in seen;seen.add(key)
     def digest(e):
      root=hashlib.blake2b(bytes.fromhex('00'+coin+e+p[2]),digest_size=32).digest()
      return int.from_bytes(hashlib.blake2b(bytes.fromhex(prev+p[4]+p[3])+root,digest_size=32).digest(),'big')
     if digest(en)<=target:prefixes.add(int.from_bytes(bytes.fromhex(p[4]),'little')>>56);accepted+=1;send({'id':q['id'],'result':True,'error':None})
     elif updated and digest('01020304')<=target:stale+=1;send({'id':q['id'],'result':False,'error':[21,'stale',None]})
     else:raise AssertionError('submitted hash does not match independent pool verifier')
  conn.close()
 except (ConnectionResetError,BrokenPipeError) as e:
  if time.monotonic()-started<3.5:errors.append(repr(e))
 except BaseException as e:errors.append(repr(e))
thread=threading.Thread(target=serve);thread.start()
cmd=[binary,'--url',f'stratum+tcp://127.0.0.1:{listener.getsockname()[1]}','--user','ocminer-test','--seconds','4','--batch','16384' if cpu else '4194304']
if cpu:cmd+=['--no-gpu','--cpu-threads','1']
elif mixed:cmd+=['--cpu-threads','4']
else:cmd+=['--no-cpu']
if '--devices' in sys.argv:
 cmd+=['--gpu-device',sys.argv[sys.argv.index('--devices')+1]]
elif not cpu and '--all-visible' not in sys.argv:cmd+=['--gpu-device','0']
p=subprocess.run(cmd,text=True,capture_output=True,timeout=15);thread.join(10);listener.close()
assert p.returncode==0,(p.returncode,p.stderr,p.stdout[-1000:])
assert not errors,errors
assert accepted>0 and epochs==1,(accepted,epochs,p.stderr,p.stdout[-1000:])
if mixed:assert 0 in prefixes and any(x>0 for x in prefixes),prefixes
print(f'PASS {"mixed CPU/CUDA" if mixed else "CPU" if cpu else "CUDA"} Stratum: {accepted} independently verified shares; extranonce change with reused job ID; {stale} in-flight stale')
