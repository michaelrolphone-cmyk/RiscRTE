#!/usr/bin/env python3
"""Verify unchanged frozen contract bytes in a supplied Runtime checkout."""
import argparse
import hashlib
import json
from pathlib import Path


def verify(repo):
    baseline=json.loads((Path(__file__).parent/'baselines.json').read_text())
    for row in baseline['files']:
        data=(Path(repo)/row['path']).read_bytes()
        blob=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
        if len(data)!=row['size'] or blob!=row['sha'] or hashlib.sha256(data).hexdigest()!=row['sha256']:
            raise ValueError('Frozen contract changed: '+row['path'])
    return len(baseline['files'])


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('--repo',type=Path,required=True)
    args=p.parse_args(); print(verify(args.repo),'frozen contract files verified.')
