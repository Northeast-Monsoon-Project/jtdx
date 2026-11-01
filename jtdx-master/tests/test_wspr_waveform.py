#!/usr/bin/env python3
"""WSPR 120 s PCM regression and paired benchmark. No rig or user log access.
The generator uses the production encoder's 162 channel symbols with continuous
phase 4-FSK; fixed PCG64 AWGN is quantized to 12 kHz mono signed-16 WAV.
"""
import argparse,collections,hashlib,json,math,os,pathlib,platform,re,statistics,subprocess,tempfile,time,wave,sys
import numpy as np
from test_modern_waveform import execute,quantile
MESSAGES=['K1ABC FN42 33','W9XYZ EN50 37','JA1ABC PM95 30','DL1ABC JO62 27','VK2ABC QF56 40','PY2ABC GG66 33']

def symbols(sim,message):
    text=subprocess.check_output([str(sim),'-c',message],text=True)
    data=[int(x) for x in text.split('Channel symbols:')[-1].split()]
    assert len(data)==162 and all(0<=n<4 for n in data),text
    return np.repeat(np.array(data,dtype=float),8192)

def waveform(sim,seed,snr,scenario,index):
    rng=np.random.default_rng(seed);pcm=rng.normal(size=120*12000);truth=[]
    if scenario=='silence':pcm.fill(0)
    elif scenario not in ('noise','noise_tone'):
        count=6 if scenario=='crowded' else 1
        for i in range(count):
            message=MESSAGES[(index+i)%len(MESSAGES)];truth.append(message)
            offset=-75+i*30 if count>1 else (-70+index%5*35)
            tones=symbols(sim,message)
            t=np.arange(len(tones))/12000
            drift=(rng.random()-.5)*1.0
            frequency=1500+offset+(tones-1.5)*12000/8192 + drift*(t/110.592-.5)
            phase=2*np.pi*np.cumsum(frequency)/12000+rng.uniform(0,2*np.pi)
            level=snr+(4 if count>1 and i%2 else 0)
            amplitude=np.sqrt(2*2500/6000*10**(level/10))
            start=int((1.6+rng.random()*.8)*12000)
            pcm[start:start+len(tones)]+=amplitude*np.sin(phase)
    if scenario in ('tone','noise_tone'):
        pcm+=.6*np.sin(2*np.pi*1503.2*np.arange(len(pcm))/12000)
    if scenario=='impulse':
        for location in rng.integers(0,len(pcm)-48,60):pcm[location:location+48]+=rng.normal(0,8,48)
    # Keep conversion away from clipping; SNR is independent of this gain.
    return np.clip(np.round(pcm*3000),-32768,32767).astype('<i2').tobytes(),sorted(truth)

def run(decoder,pcm,truth,args,seed,snr,scenario,index,label):
    with tempfile.TemporaryDirectory(prefix='jtdxf-wspr-') as td:
        folder=pathlib.Path(td);wav=folder/'261031_1200.wav'
        with wave.open(str(wav),'wb') as w:w.setnchannels(1);w.setsampwidth(2);w.setframerate(12000);w.writeframes(pcm)
        command=[str(decoder),'-a',str(folder),'-f','14.0956','-C','5000','-o','4',str(wav)]
        worker=pathlib.Path(__file__).with_name('measure_decoder_process.py').resolve()
        measured=json.loads(subprocess.check_output([sys.executable,str(worker),str(args.timeout),*command],cwd=folder,text=True))
        wall=measured['wall_seconds']
        if measured['returncode'] or '<DecodeFinished>' not in measured['stdout']:
            raise RuntimeError(f'{command}: {measured}')
        decoded=set()
        for line in measured['stdout'].splitlines():
            fields=line.split()
            if len(fields)>=7 and re.fullmatch(r'\d{4}',fields[0]):decoded.add(' '.join(fields[5:]))
        expected=set(truth)
        record=dict(label=label,seed=seed,snr=snr,scenario=scenario,index=index,truth=truth,decoded=sorted(decoded),
                    good=sorted(expected&decoded),wrong=sorted(decoded-expected),missed=sorted(expected-decoded),wall_seconds=wall)
        for key in ('cpu_seconds','peak_rss_kib'):
            if key in measured:record[key]=measured[key]
        return record

def summarize(records):
    groups=collections.defaultdict(list)
    for r in records:groups[r['label'],r['scenario'],r['snr']].append(r)
    out=[]
    for (label,scenario,snr),rows in sorted(groups.items()):
        wall=[r['wall_seconds'] for r in rows];cpu=[r.get('cpu_seconds',0) for r in rows]
        out.append(dict(label=label,scenario=scenario,snr=snr,frames=len(rows),truth=sum(len(r['truth']) for r in rows),
                        correct=sum(len(r['good']) for r in rows),wrong=sum(len(r['wrong']) for r in rows),
                        wall_mean=statistics.mean(wall),wall_p95=quantile(wall,.95),wall_max=max(wall),cpu_total=sum(cpu),cpu_mean=statistics.mean(cpu),
                        peak_rss_kib=max(r.get('peak_rss_kib',0) for r in rows)))
    return out

def main():
    p=argparse.ArgumentParser();p.add_argument('--decoder',type=pathlib.Path,required=True);p.add_argument('--baseline',type=pathlib.Path)
    p.add_argument('--sim',type=pathlib.Path,required=True);p.add_argument('--seed',type=int,default=20261101);p.add_argument('--trials',type=int,default=10)
    p.add_argument('--scenarios',nargs='+',default=['awgn','impulse','crowded']);p.add_argument('--snrs',nargs='+',type=float,default=[-25,-28,-30])
    p.add_argument('--output',type=pathlib.Path);p.add_argument('--timeout',type=int,default=90);p.add_argument('--benchmark',action='store_true');args=p.parse_args()
    args.decoder=args.decoder.resolve();args.sim=args.sim.resolve()
    if args.baseline:args.baseline=args.baseline.resolve()
    records=[];plans=[(scenario,snr,index) for scenario in args.scenarios for snr in args.snrs for index in range(args.trials)] if args.benchmark else [('awgn',-10,0),('silence',0,1),('noise',0,2)]
    for scenario,snr,index in plans:
        seed=args.seed+index*104729;pcm,truth=waveform(args.sim,seed,snr,scenario,index)
        variants=[('upgraded',args.decoder)]+([('baseline',args.baseline)] if args.baseline else [])
        if index%2:variants.reverse()
        pair=[]
        for label,decoder in variants:
            record=run(decoder,pcm,truth,args,seed,snr,scenario,index,label);records.append(record);pair.append(record)
            if not args.benchmark:assert record['good']==truth and not record['wrong'],record
        print(json.dumps({'scenario':scenario,'snr':snr,'index':index,'runs':[{k:r[k] for k in ('label','good','wrong','wall_seconds')} for r in pair]}),flush=True)
        if args.output:
            report={'seed':args.seed,'generator':'PCG64 / continuous-phase 4-FSK / 12k PCM','machine':platform.uname()._asdict(),
                    'decoder_sha256':hashlib.sha256(args.decoder.read_bytes()).hexdigest(),'records':records,'summary':summarize(records)}
            if args.baseline:report['baseline_sha256']=hashlib.sha256(args.baseline.read_bytes()).hexdigest()
            args.output.write_text(json.dumps(report,indent=2))
    print(json.dumps(summarize(records),indent=2))
if __name__=='__main__':main()
