#!/usr/bin/env python3
"""Compile exact selected ABI sizes without linking a layout probe into firmware."""
import argparse,json,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('manifest',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];m=json.loads(a.manifest.read_text());cmd=next(c.copy() for c in m['commands'] if '-c' in c and any(x.endswith('/src/bootstrap/Runtime.cpp') for x in c));cmd[cmd.index('-c')+1]=str(root/'test/native_settings_target_layout.cpp');a.output.parent.mkdir(parents=True,exist_ok=True);obj=a.output.with_suffix('.o').resolve();cmd[cmd.index('-o')+1]=str(obj);cmd.append('-fno-access-control');subprocess.run(cmd,cwd=root,check=True)
nm=subprocess.check_output([cmd[0].removesuffix('g++')+'nm','-S',str(obj)],text=True);a.output.with_suffix('.nm').write_text(nm);sizes={r.split()[-1][9:]:int(r.split()[1],16) for r in nm.splitlines() if r.split()[-1].startswith('resource_')};a.output.write_text(json.dumps(sizes,indent=2,sort_keys=True)+'\n');a.output.with_suffix('.command.json').write_text(json.dumps(cmd,indent=2)+'\n');print(sizes)
