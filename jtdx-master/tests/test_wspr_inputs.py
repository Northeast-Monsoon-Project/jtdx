#!/usr/bin/env python3
"""Process-level malformed input tests; uses no radio or personal logs."""
import pathlib,subprocess,sys,tempfile,struct,wave
exe=str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='jtdxf-wspr-input-') as td:
    root=pathlib.Path(td)
    def run(path,expected=1,extra=()):
        p=subprocess.run([exe,'-a',str(root),*extra,str(path)],capture_output=True,timeout=15)
        assert p.returncode==expected,(path,p.returncode,p.stdout,p.stderr)
    for data in (b'',b'RIFF',b'RIFF'+struct.pack('<I',10000)+b'WAVE',b'not a wav'):
        path=root/'x.wav';path.write_bytes(data);run(path)
    path=root/'x.c2';path.write_bytes(b'bad');run(path)
    path.write_bytes(bytes(14)+struct.pack('<id',2,float('nan'))+bytes(360000));run(path)
    path.write_bytes(bytes(14)+struct.pack('<id',2,14.0956)+struct.pack('<f',float('nan'))+bytes(359996));run(path)
    with wave.open(str(root/'x.WAV'),'wb') as w:w.setnchannels(1);w.setsampwidth(2);w.setframerate(12000);w.writeframes(bytes(24000))
    (root/'hashtable.txt').write_text('-1 BAD XX00\n32768 BAD XX00\n999999999999999999999999 BIG XX00\n4 CALLTOOLONGFORBUFFERXXXXXXXXXXXXXXXXXXXXXX ABCDEFGHIJ\n')
    run(root/'x.WAV',0)
    for option in (('-o','99'),('-C','-1'),('-e','nan'),('-z','inf')):run(root/'x.WAV',1,option)
    (root/'wspr_spots.txt').unlink();(root/'wspr_spots.txt').mkdir();run(root/'x.WAV')
print('WSPR malformed headers, C2 NaN, short names, hash bounds, option and output-file tests passed')
