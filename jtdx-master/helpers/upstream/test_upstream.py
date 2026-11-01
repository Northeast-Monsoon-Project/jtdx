#!/usr/bin/env python3
"""Offline protocol, Q65 PCM, averaging, Echo and astronomy integration checks."""
import argparse,array,json,math,os,pathlib,random,select,shutil,subprocess,sys,tempfile

class Backend:
 def __init__(self,exe,wine=False,env=None,workdir=None):
  self.stderr=tempfile.TemporaryFile()
  self.wine=wine
  command=(['wine'] if wine else [])+[str(exe)]
  arguments=[] if workdir is None else ['--work-dir',('Z:' if wine else '')+str(workdir)]
  command+=arguments
  if wine and env is not None:
   # Wine intentionally replaces TEMP/TMP from the Unix environment. Set the
   # Unicode Windows paths through cmd's Unicode command line, so the test
   # actually exercises a non-ASCII Windows user temp directory.
   values={key:env[key] for key in ('TEMP','TMP') if key in env}
   if any(any(c in value for c in '\r\n"&|%<>!') for value in values.values()):raise ValueError('unsafe test temp path')
   if arguments:raise ValueError('Wine TEMP override and explicit workdir are separate test cases')
   windows_parent='Z:'+str(exe.parent) if str(exe.parent).startswith('/') else str(exe.parent)
   # cmd accepts spaces in SET values and CD paths. Avoid embedded quotes in
   # its /c argument: Wine escapes those for normal Windows argv parsing,
   # whereas cmd has its own command-line grammar.
   commands=['set '+key+'='+value for key,value in values.items()]+['cd /d '+windows_parent,exe.name]
   command=['wine','cmd','/d','/c','&'.join(commands)]
  self.proc=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.stderr,bufsize=0,env=env)
  self.next=0
 def request(self,op,**values):
  self.next+=1
  req=dict(api=1,id=str(self.next),context='synthetic-test',op=op);req.update(values)
  # The Python harness owns the Linux temporary files. Wine's Z: drive
  # exposes those same files to the actual PE helper, without a fake backend.
  if self.wine:
   for key in ('path','ephemeris_path'):
    if key in req and str(req[key]).startswith('/'):req[key]='Z:'+str(req[key])
  self.proc.stdin.write(json.dumps(req).encode()+b'\n');self.proc.stdin.flush();reply=[]
  while True:
   if not select.select([self.proc.stdout],[],[],60)[0]:raise AssertionError('helper timeout')
   line=self.proc.stdout.readline()
   if not line:
    self.stderr.seek(0);raise AssertionError('helper terminated: '+self.stderr.read().decode(errors='replace'))
   result=json.loads(line);assert result['id']==req['id'] and result['context']==req['context'],result
   reply.append(result)
   if result['type'] in ('done','encoded','astro','hello','error'):return reply
 def close(self):
  self.proc.stdin.close()
  try:self.proc.wait(timeout=5)
  except subprocess.TimeoutExpired:self.proc.kill();self.proc.wait()
  self.stderr.close()

def pcm_read(path):
 x=array.array('h');x.frombytes(path.read_bytes())
 if sys.byteorder!='little':x.byteswap()
 return x

def pcm_write(path,x):
 a=array.array('h',x)
 if sys.byteorder!='little':a.byteswap()
 path.write_bytes(a.tobytes())

def main():
 parser=argparse.ArgumentParser();parser.add_argument('helper',type=pathlib.Path);parser.add_argument('ephemeris',type=pathlib.Path)
 parser.add_argument('--wine',action='store_true',help='Run the Windows PE helper through Wine; map Linux PCM/ephemeris paths to Z:.')
 args=parser.parse_args();b=Backend(args.helper.resolve(),args.wine);count=0
 try:
  h=b.request('hello')[0];assert h['version']=='3.0.2' and 'Q65' in h['modes'];count+=1
  with tempfile.TemporaryDirectory(prefix='jtdxf-upstream-test-') as d:
   root=pathlib.Path(d);tx=root/'tx.pcm';rx=root/'rx.pcm';rng=random.Random(360021)
   encoded=b.request('encode',mode='Q65',message='CQ K1ABC FN20',period=15,submode=1,audio_hz=1500,path=str(tx))[0]
   assert encoded['type']=='encoded' and encoded['sample_rate']==48000 and encoded['start_delay_ms']==500,encoded
   assert tx.stat().st_size==encoded['sample_count']*2;count+=1
   signal=pcm_read(tx)[::4];noise=[round(rng.gauss(0,1000)) for _ in range(180000)]
   for i,s in enumerate(signal):noise[6000+i]+=round(s*.1)
   pcm_write(rx,noise)
   opts=dict(mode='Q65',period=15,submode=1,path=str(rx),sample_count=180000,utc=120000,rx_hz=1500,tolerance=100,average=True,clear_average=True)
   result=b.request('decode',**opts)
   assert any(r.get('message')=='CQ K1ABC FN20' for r in result),result
   assert result[-1]['type']=='done',result;count+=1
   opts.update(utc=120030,clear_average=False)
   second=b.request('decode',**opts)
   assert second[-1]['average_even']>=result[-1]['average_even'],(result,second);count+=1
   # The full active Q65-15 payload ends at13.25 s. A13.45 s snapshot must
   # decode before the next TX slot, with the unused tail zero-padded by helper.
   pcm_write(rx,noise[:161400]);early=dict(opts,sample_count=161400,utc=120100,clear_average=True)
   early_result=b.request('decode',**early)
   assert any(r.get('message')=='CQ K1ABC FN20' for r in early_result),early_result;count+=1
   # Q65-15 EME inherits upstream's15 s input window: the last0.75 s is
   # outside it with2.5 s EME delay. Verify a strong truncated waveform can
   # decode; this does not establish equal weak-signal sensitivity.
   delayed=[round(rng.gauss(0,1000)) for _ in range(180000)]
   for i,s in enumerate(signal[:144000]):delayed[36000+i]+=round(s*.1)
   pcm_write(rx,delayed)
   delayed_result=b.request('decode',**dict(opts,utc=120115,clear_average=True,eme_delay=2.5))
   assert any(r.get('message')=='CQ K1ABC FN20' for r in delayed_result),delayed_result;count+=1
   pcm_write(rx,noise)
   bad=b.request('decode',mode='Q65',period=15,path=str(rx),sample_count=179999)[0]
   assert bad['type']=='error' and bad['code']=='invalid_pcm_file',bad;count+=1
   assert b.request('encode',mode='Q65',period=16,path=str(tx),message='CQ K1ABC FN20')[0]['type']=='error';count+=1
   for period,submode in [(15,3),(30,4),(60,5),(300,5)]:
    assert b.request('encode',mode='Q65',period=period,submode=submode,path=str(tx),message='CQ K1ABC FN20')[0]['type']=='error';count+=1
    assert b.request('decode',mode='Q65',period=period,submode=submode,path=str(rx),sample_count=180000)[0]['type']=='error';count+=1
   assert b.request('decode',mode='Q65',context='another-context')[0]['code']=='context_mismatch';count+=1
   m=b.request('encode',mode='MSK144',period=15,message='CQ K1ABC FN20',path=str(tx))[0]
   assert m['type']=='encoded' and m['sample_count']==696000 and m['start_delay_ms']==0,m;count+=1
   signal=pcm_read(tx)[::4][:7168]
   pcm_write(rx,[round(s*.2+rng.gauss(0,100)) for s in signal])
   m=b.request('decode',mode='MSK144',path=str(rx),sample_count=7168,utc=120000,tsec=0,my_call='W9XYZ',his_call='K1ABC')
   assert any(r.get('message')=='CQ K1ABC FN20' for r in m),m;count+=1
   assert b.request('decode',mode='MSK144',path=str(rx),sample_count=7167)[0]['type']=='error';count+=1
   e=b.request('encode',mode='Echo',audio_hz=1500,path=str(tx))[0]
   assert e['type']=='encoded' and e['sample_count']==98304 and e['start_delay_ms']==0,e;count+=1
   # Echo receives the post-TX 2.048 s capture; a known tone must remain finite.
   noise=[round(1000*math.sin(2*math.pi*1500*i/12000)+rng.gauss(0,100)) for i in range(7*4096)]
   pcm_write(rx,noise)
   echo=b.request('decode',mode='Echo',path=str(rx),sample_count=len(noise),averages=2,clear_average=True)
   assert echo[0]['type']=='echo' and echo[0]['integrations']==1 and echo[-1]['type']=='done',echo;count+=1
   astro=b.request('astro',utc_ms=1793332800000,tx_utc_ms=1793332830000,frequency_hz=144000000,my_grid='FN20',his_grid='JN18',ephemeris_path=str(args.ephemeris.resolve()))[0]
   assert astro['type']=='astro' and 2<astro['echo_delay_seconds']<3,astro
   assert -90<=astro['moon_el_deg']<=90 and 'tx_self_doppler_hz' in astro,astro;count+=1
   assert b.request('astro',my_grid='FN20')[0]['type']=='error';count+=1
   # First astronomy call in a fresh process must work when both the installed
   # ephemeris path and the user's temporary directory need Unicode. A second
   # call in an already initialized JPL process would conceal the old OPEN bug.
   unicode_root=root/'安装路径_é_測試';unicode_root.mkdir()
   ephemeris=unicode_root/'JPLEPH';shutil.copy2(args.ephemeris.resolve(),ephemeris)
   scratch=unicode_root/'用户临时目录';scratch.mkdir()
   env=os.environ.copy();native_temp=('Z:' if args.wine else '')+str(scratch)
   env.update(TEMP=native_temp,TMP=native_temp,TMPDIR=str(scratch))
   unicode_backend=Backend(args.helper.resolve(),args.wine,env)
   try:
    value=unicode_backend.request('astro',utc_ms=1793332800000,tx_utc_ms=1793332830000,frequency_hz=144000000,my_grid='FN20',his_grid='JN18',ephemeris_path=str(ephemeris))[0]
    assert value['type']=='astro' and value['echo_delay_seconds']==astro['echo_delay_seconds'],value
    assert value['tx_self_doppler_hz']==astro['tx_self_doppler_hz'],value
    assert any(p.is_dir() and (p/'JPLEPH').exists() for p in scratch.iterdir()),'Unicode scratch directory was not exercised'
    count+=1
   finally:unicode_backend.close()
   assert not list(scratch.iterdir()),'Helper left its staged ephemeris behind';count+=1
   # The GUI owns this directory, so a forcibly stopped child cannot leak its
   # copied ephemeris outside the parent-owned tree.
   owned=unicode_root/'父进程工作目录';owned.mkdir()
   managed=Backend(args.helper.resolve(),args.wine,workdir=owned)
   try:
    assert managed.request('hello')[0]['managed_work_dir']
    value=managed.request('astro',utc_ms=1793332800000,frequency_hz=144000000,my_grid='FN20',his_grid='JN18',ephemeris_path=str(ephemeris))[0]
    assert value['type']=='astro' and (owned/'JPLEPH').exists(),value;count+=1
    managed.proc.kill();managed.proc.wait(timeout=5)
   finally:managed.close()
   shutil.rmtree(owned);assert not owned.exists();count+=1
  assert b.request('reset')[0]['restart_required'];count+=1
 finally:b.close()
 print(json.dumps({'checks':count,'result':'PASS','version':'3.0.2','runtime':'wine' if args.wine else 'native'},indent=2))
if __name__=='__main__':main()
