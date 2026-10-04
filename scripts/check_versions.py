#!/usr/bin/env python3
"""Source-version gate adapted from Reader's independent version checks."""
import argparse, configparser, json, re, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def version(value):
    if not isinstance(value,str) or not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)',value):
        raise ValueError('version must be canonical major.minor.patch')
    result=tuple(map(int,value.split('.')))
    if max(result)>65535: raise ValueError('version component exceeds 65535')
    return result
def firmware(text):
    c=configparser.ConfigParser(interpolation=None);c.read_string(text)
    value=c.get('riscrte','version');version(value);return value
def at(ref,path):
    result=subprocess.run(['git','show',f'{ref}:{path}'],cwd=ROOT,capture_output=True,text=True)
    return result.stdout if result.returncode==0 else None
def check(base=None):
    current=firmware((ROOT/'platformio.ini').read_text())
    app=json.loads((ROOT/'apps/heartbeat/manifest.json').read_text());version(app['version'])
    if (app['id'],app['file_name'],app['architecture'])!=('riscrte-heartbeat','default.elf','xtensa-esp32s3'):
        raise ValueError('heartbeat manifest identity mismatch')
    if f'RTE_HEARTBEAT version={app["version"]} ' not in (ROOT/'apps/heartbeat/main.c').read_text():
        raise ValueError('heartbeat line version differs from its manifest')
    if base:
        changed=subprocess.check_output(['git','diff','--name-only',base,'HEAD'],cwd=ROOT,text=True).splitlines()
        previous=at(base,'platformio.ini')
        # First standalone migration has no firmware version in its seed.
        if previous and '[riscrte]' in previous:
            old=firmware(previous)
            if version(current)<version(old):raise ValueError('firmware version regressed')
            if any(p.startswith(('src/','lib/','sdk/','data/','apps/')) or p in ('platformio.ini','partitions.csv','requirements-ci.txt','scripts/build_apps.py','scripts/embed_store.py','scripts/reproducible_build.py') for p in changed) and version(current)<=version(old):
                raise ValueError('changed firmware inputs require a higher firmware version')
        previous=at(base,'apps/heartbeat/manifest.json')
        if previous:
            old=json.loads(previous)['version']
            if version(app['version'])<version(old):raise ValueError('heartbeat version regressed')
            if any(p.startswith('apps/heartbeat/') for p in changed) and version(app['version'])<=version(old):
                raise ValueError('changed heartbeat source/manifest requires a higher app version')
    print('Source firmware/app version guards passed:',current,app['version'])
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--base');a=p.parse_args();check(a.base)
