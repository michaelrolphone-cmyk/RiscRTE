#!/usr/bin/env python3
"""Compare actual default-off main/Runtime target objects to the fixed integration base."""
import argparse,hashlib,json,pathlib,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('target_output',type=pathlib.Path);p.add_argument('--baseline',default='3795a60');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];out=a.target_output.resolve();commands=json.loads((out/'commands.json').read_text());rows=[]
with tempfile.TemporaryDirectory(dir=out) as d:
 source=pathlib.Path(d)/'source.cpp';obj=pathlib.Path(d)/'source.o'
 for stem in ('main','Runtime'):
  for layout in ('legacy','cohort'):
   cmd=next(c for c in commands if pathlib.Path(c[-1]).name==stem+'-'+layout+'.o')
   at=cmd.index('-c');relative=pathlib.Path(cmd[at+1]).relative_to(root)
   flags=[x for x in cmd[:at] if not x.startswith(('-DRISC_NATIVE_SETTINGS_LIFECYCLE=','-DRISC_NATIVE_SETTINGS_WORKER=','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=','-DRISC_NATIVE_STORAGE_CUSTODY='))]
   flags+=['-I'+str(root/'src/bootstrap')]
   hashes=[]
   for old in (True,False):
    source.write_bytes(subprocess.check_output(['git','show',a.baseline+':'+str(relative)],cwd=root) if old else (root/relative).read_bytes())
    subprocess.run([*flags,'-c',str(source),'-o',str(obj)],check=True)
    hashes.append(hashlib.sha256(obj.read_bytes()).hexdigest())
   assert hashes[0]==hashes[1],(str(relative),layout,hashes)
   rows.append(dict(source=str(relative),layout=layout,baseline=a.baseline,sha256=hashes[0],byte_identical=True))
(out/'default-off-comparison.json').write_text(json.dumps(rows,indent=2)+'\n')
print('Actual default-off main and Runtime objects byte-identical to '+a.baseline+' in legacy/cohort layouts')
