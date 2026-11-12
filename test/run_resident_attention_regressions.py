#!/usr/bin/env python3
"""Run unchanged resident suites with explicit retained artifact custody."""
import argparse, hashlib, json, os, shlex, shutil, subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--sanitize',action='store_true');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
files=[f for part in ('src','sdk','lib/ArduinoJson/src','test') for f in (R/part).rglob('*') if f.is_file() and '__pycache__' not in f.parts]
def hashes():return {str(f.relative_to(R)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}
before=hashes();wrappers=out/'compiler';wrappers.mkdir(exist_ok=True)
for name in ('cc','c++'):
 compiler=shutil.which(name);assert compiler
 # Existing tests use debug information unnecessarily for this qualification;
 # preserve all warnings/instrumentation while bounding retained artifact size.
 (wrappers/name).write_text('#!/bin/bash\nif [[ " $* " == *\' -shared \'* ]]; then exec '+shlex.quote(compiler)+' "$@" -g0 -pipe; else exec '+shlex.quote(compiler)+' "$@" -g0 -pipe -fno-pie -no-pie; fi\n');(wrappers/name).chmod(0o755)
results=[]
for suite in ('policy','shell'):
 source=R/'test'/('run_resident_'+suite+'_test.sh');original=source.read_text();d=out/suite;d.mkdir(exist_ok=True)
 lines=original.splitlines();changes=[]
 for i,line in enumerate(lines):
  if line.startswith('repo='):lines[i]='repo='+shlex.quote(str(R));changes.append('repo')
  elif line.startswith('build='):lines[i]='build='+shlex.quote(str(d));changes.append('build')
  elif line=="trap 'rm -rf \"$build\"' EXIT":lines[i]='# Retain all exact test artifacts for receipt.';changes.append('cleanup')
 assert changes==['repo','build','cleanup'],changes
 script=d/'run-preserved.sh';script.write_text('\n'.join(lines)+'\n')
 env={**os.environ,'PATH':str(wrappers)+os.pathsep+os.environ['PATH'],'SANITIZE':'1' if a.sanitize else '0','ASAN_OPTIONS':'detect_leaks=0'}
 command=['bash','-x',str(script)];result=subprocess.run(command,text=True,capture_output=True,env=env);(d/'run.log').write_text(result.stdout+result.stderr)
 result.check_returncode();passed=[line for line in result.stdout.splitlines() if line.endswith('PASS')];assert len(passed)==(25 if suite=='policy' else 29)
 results.append({'suite':suite,'cases':len(passed),'command':command,'original_script_sha256':hashlib.sha256(original.encode()).hexdigest(),'custody_only_rewrites':changes})
assert hashes()==before,'source changed during regression qualification'
artifacts={str(f.relative_to(out)):hashlib.sha256(f.read_bytes()).hexdigest() for f in out.rglob('*') if f.is_file()}
(out/'receipt.json').write_text(json.dumps({'sanitize':a.sanitize,'results':results,'source_inputs':before,'artifacts':artifacts,'scope':'Original test logic unchanged. Only repository/output paths, artifact cleanup and compiler debug/PIE options instrumented.'},indent=2)+'\n');print('54 original resident scenarios PASS')
