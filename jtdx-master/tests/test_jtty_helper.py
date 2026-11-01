#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""End-to-end JTTY subprocess PCM encode/receive and validation test.
Arguments are the executable command; pass wine before a Windows .exe.
"""
import subprocess, json, base64, sys
p=subprocess.Popen(sys.argv[1:],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
seq=0
def request(**kw):
 global seq
 seq+=1;kw['id']=str(seq)
 p.stdin.write(json.dumps(kw)+'\n');p.stdin.flush()
 result=json.loads(p.stdout.readline());assert result['id']==str(seq)
 return result
r=request(op='encode',text='CQ K1ABC FN20',rate=12000,frequency=1500)
assert 'error' not in r,r
print('encode',r['frames'],r['samples'],r['text'],flush=True)
pcm=base64.b64decode(r['pcm'])
updates=[]
request(op='reset')
signal=b'\0'*24000+pcm+b'\0'*24000*12
for i in range(0,len(signal),24000):
 block=signal[i:i+24000]
 r=request(op='audio',pcm=base64.b64encode(block).decode(),low=200,high=3000,center=1500)
 assert 'error' not in r,r
 updates.extend(r.get('updates',[]))
print('updates',updates,flush=True)
assert any(u['text'].strip()=='CQ K1ABC FN20' and u['terminal'] for u in updates),updates
for args in [dict(text='中文',rate=12000),dict(text='A'*81,rate=12000),dict(text='OK',rate=1),dict(text='OK',frequency=0)]:
 assert 'error' in request(op='encode',**args)
p.stdin.close();assert p.wait(timeout=5)==0
print('PASS roundtrip and invalid requests')
