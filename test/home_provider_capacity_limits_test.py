#!/usr/bin/env python3
"""Compile actual bounds; preserve defaults/legacy and reject 49 or shrinking."""
import pathlib,subprocess,tempfile
r=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 root=pathlib.Path(tmp);source=root/'limits.cpp'
 for name,flags,providers,grants,valid in [
  ('default',[],28,44,True),('installed',['-DRISC_INSTALLED_APP_CAPACITY=64'],29,45,True),
  ('forty',['-DRISC_PROVIDER_CAPACITY=40'],40,56,True),
  ('home',['-DRISC_PROVIDER_CAPACITY=48'],48,64,True),
  ('legacy',['-DESP_PLATFORM','-DRISC_PROVIDER_CAPACITY=48'],17,32,True),
  ('overflow',['-DRISC_PROVIDER_CAPACITY=49'],49,65,False),
  ('shrink',['-DRISC_INSTALLED_APP_CAPACITY=64','-DRISC_PROVIDER_CAPACITY=28'],28,44,False)]:
  source.write_text('#include "runtime/RuntimeLimits.h"\nstatic_assert(RiscLimits::Providers=='+str(providers)+' && RiscLimits::Grants=='+str(grants)+',"bounds");\nint main(){}\n')
  q=subprocess.run(['c++','-std=c++17','-I'+str(r/'src'),*flags,str(source),'-o',str(root/name)],capture_output=True,text=True)
  assert (q.returncode==0)==valid,(name,q.stdout,q.stderr)
 print('Actual default/installed/40/48/legacy bounds and invalid49/shrink PASS')
