#!/usr/bin/env python3
"""Archive the committed source, workspace build helpers and dependency manifests."""
from pathlib import Path
import subprocess
import zipfile
import re
root=Path(__file__).resolve().parent.parent
source=root/'jtdx-master'
release=root/'release';release.mkdir(exist_ok=True)
if subprocess.check_output(['git','status','--porcelain'],cwd=source):
    raise SystemExit('Commit source changes before preparing the source archive')
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip()
files=subprocess.check_output(['git','ls-files','-z'],cwd=source).decode().split('\0')
helpers=['env.sh','gfortran-local','run-linux.sh','build-windows.sh','windows-toolchain.cmake','pkg-config-windows','package_windows.py','make-windows-installer.sh','windows-installer.nsi','download_windows_ucrt_deps.py','make_source_release.py']
version_source=(source/'Versions.cmake').read_text()
version='.'.join(re.search(r'set \(WSJTX_VERSION_'+key+r' (\d+)\)',version_source).group(1) for key in ('MAJOR','MINOR','SUB'))
label='JTDX-F-V'+version
output=release/(label+'-Source.zip')
with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for f in files:
        if not f: continue
        if Path(f).suffix.lower() in ('.adi','.adif','.log','.pyc','.o','.obj','.a','.exe','.dll','.pdb') or any(part in ('.qa','__pycache__') for part in Path(f).parts): raise SystemExit('Private/log/generated file cannot enter source package: '+f)
        z.write(source/f,label+'/jtdx-master/'+f)
    for name in helpers:z.write(root/'scripts'/name,label+'/scripts/'+name)
    for p in (root/'dependencies').glob('*'):
        if p.is_file():z.write(p,label+'/dependencies/'+p.name)
    z.writestr(label+'/SOURCE_COMMIT.txt',commit+'\n')
    z.writestr(label+'/README.txt','JTDX-F V'+version+' by Fluxmancer\nSee jtdx-master/docs/构建说明_zh.md for dependencies and build commands.\nSource: jtdx-master/; workspace build helpers: scripts/.\nLicense: jtdx-master/COPYING (GNU GPLv3).\n')
print(str(output),output.stat().st_size,'bytes; source commit',commit)
