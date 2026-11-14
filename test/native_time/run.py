#!/usr/bin/env python3
"""Owning arithmetic/clock adapter tests; optional exact native consumer compile."""
import argparse,hashlib,json,os,pathlib,shlex,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--compile-commands',type=pathlib.Path);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);(out/'tmp').mkdir(exist_ok=True);env={**os.environ,'TMPDIR':str(out/'tmp'),'ASAN_OPTIONS':'detect_leaks=0'}
def run(cmd,**kw):
 r=subprocess.run([str(x) for x in cmd],env=env,text=True,capture_output=True,**kw)
 if r.returncode:
  (out/'failure.json').write_text(json.dumps({'command':list(map(str,cmd)),'stdout':r.stdout,'stderr':r.stderr},indent=2));raise RuntimeError(r.stdout+r.stderr)
 return r.stdout
records=[]
for native in [0,1]:
 for san in [False,True]:
  exe=out/('test-%d-%s'%(native,'sanitized' if san else 'normal'))
  cmd=['g++','-O2','-std=c++17','-Wall','-Wextra','-Werror',f'-DRISC_NATIVE_ESP_IDF={native}','-I'+str(ROOT/'src/ports/esp32s3'),'-I'+str(ROOT/'test/native_time/stubs'),ROOT/'test/native_time/quotient_test.cpp','-o',exe]
  if san:cmd[1:1]=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie']
  run(cmd);records.append({'command':list(map(str,cmd)),'stdout':run([exe])})
if a.compile_commands:
 entries=json.loads(a.compile_commands.read_text());entry=next(x for x in entries if x['file'].endswith('/src/main.cpp'))
 old_src=str(pathlib.Path(entry['file']).parent);cmd=[x.replace(old_src,str(ROOT/'src')) for x in shlex.split(entry['command'])];cmd[cmd.index('-c')+1]=str(ROOT/'src/main.cpp');cmd[cmd.index('-o')+1]=str(out/'main.o');cmd+=['-MMD','-MF',str(out/'main.d')];run(cmd,cwd=entry['directory'])
 tool=cmd[0][:-3];dis=run([tool+'objdump','-dr',out/'main.o']);(out/'main-disassembly.txt').write_text(dis)
 # Inspect the actual callback, not a substitute arithmetic-only wrapper.
 sections=dis.split('Disassembly of section ')
 selected=[s for s in sections if 'monotonicMillis' in s.split('\n')[0]]
 assert selected,'missing actual monotonic callback'
 text='\n'.join(selected);(out/'monotonic-disassembly.txt').write_text(text)
 assert '__udivdi3' not in text
 millis='\n'.join(s for s in sections if 'RiscPlatform6millis' in s.split('\n')[0]);assert '__udivdi3' not in millis and 'esp_timer_get_time' in text+millis
 assert 'muluh' in text+millis
 (out/'millis-disassembly.txt').write_text(millis)
 # If compiler chooses shared helper, its body must also be divider-free.
 helper='\n'.join(s for s in sections if 'unsignedMicrosToMillis32' in s.split('\n')[0]);assert '__udivdi3' not in helper
 deps=(out/'main.d').read_text().replace('\\\n',' ').split(':',1)[1].split();assert str(ROOT/'src/ports/esp32s3/NativeTime.h') in deps
 hashes={f:hashlib.sha256(pathlib.Path(f).read_bytes()).hexdigest() for f in deps if pathlib.Path(f).is_file()}
 (out/'target-dependencies.json').write_text(json.dumps(hashes,indent=2));records.append({'command':cmd,'cwd':entry['directory'],'callback_disassembly':text,'helper_disassembly':helper,'millis_disassembly':millis,'compiler':run([cmd[0],'--version']),'sizes':run([tool+'nm','-S','--size-sort',out/'main.o'])})
inputs=list((ROOT/'test/native_time').rglob('*'))+[ROOT/'src/ports/esp32s3/NativeTime.h',ROOT/'src/ports/esp32s3/NativeTimeQuotient.h']
e={'commit':run(['git','-C',ROOT,'rev-parse','HEAD']).strip(),'dirty':bool(run(['git','-C',ROOT,'status','--porcelain']).strip()),'results':records,'source_hashes':{str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs if f.is_file()}}
(out/'evidence.json').write_text(json.dumps(e,indent=2));print('PASS four host lanes'+(' and exact native main.cpp callback compile' if a.compile_commands else ''))
