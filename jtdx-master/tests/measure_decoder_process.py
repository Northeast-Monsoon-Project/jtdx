#!/usr/bin/env python3
"""Small launcher to keep a waveform generator's resident arrays out of RSS.
The child decoder is forked only after this Python process has exec'ed.
"""
import json,sys,time
from test_modern_waveform import execute
start=time.perf_counter()
p,u=execute(sys.argv[2:],'.',int(sys.argv[1]))
result=dict(returncode=p.returncode,stdout=p.stdout,stderr=p.stderr,wall_seconds=time.perf_counter()-start)
if u:result.update(cpu_seconds=u.ru_utime+u.ru_stime,peak_rss_kib=u.ru_maxrss)
print(json.dumps(result))
