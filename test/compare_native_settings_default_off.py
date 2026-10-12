#!/usr/bin/env python3
"""Compare actual main/Runtime default-off objects using recorded .99 commands."""
import argparse,hashlib,io,json,pathlib,subprocess,tarfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('manifest',type=pathlib.Path);p.add_argument('output',type=pathlib.Path)
p.add_argument('--base',default='0817be16ac8c8093cb677c18106ef188e5505686')
a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[1];out=a.output.resolve()
out.mkdir(parents=True,exist_ok=False);base=out/'base';base.mkdir()
data=subprocess.check_output(['git','archive',a.base],cwd=root)
tarfile.open(fileobj=io.BytesIO(data)).extractall(base,filter='data')
m=json.loads(a.manifest.read_text());records=[]
for source,stem in [('src/bootstrap/Runtime.cpp','runtime-off'),('src/main.cpp','main-off')]:
 original=next(c for c in m['calls'] if c[-1].endswith('/'+stem+'.o'))
 assert str(root/source) in original,'Manifest must describe this source root'
 artifacts=[]
 for label,srcroot in [('base',base),('candidate',root)]:
  cmd=[x.replace(str(root),str(srcroot)) for x in original]
  dest=out/(label+'-'+stem+'.o');cmd[-1]=str(dest)
  cmd=[x for x in cmd if x!='-ggdb']
  cmd[1:1]=['-g0','-frandom-seed=default-off-comparison']
  subprocess.run(cmd,cwd=srcroot,check=True);artifacts.append(dest)
  records.append({'source':source,'revision':a.base if label=='base' else subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'command':cmd,'object_sha256':hashlib.sha256(dest.read_bytes()).hexdigest()})
 assert artifacts[0].read_bytes()==artifacts[1].read_bytes(),stem+' changed default-off code/data'
 print(stem,'default-off whole -g0 object identical to base',a.base,flush=True)
(out/'comparison.json').write_text(json.dumps({'records':records,'basis':'Actual full .99 flags plus -g0 and fixed random seed; entire object identity, no final firmware link'},indent=2)+'\n')
