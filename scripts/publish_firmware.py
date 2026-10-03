#!/usr/bin/env python3
"""Version-gated publication, only after verified integration artifacts exist.
Uses GitHub Actions' GITHUB_TOKEN; never overwrites a published version.
"""
import argparse,json,os,subprocess,tempfile
from pathlib import Path
from release_assets import ROOT,OUTPUT,verify,require,file_bytes
from check_versions import version

def gh(*args):
    return subprocess.check_output(['gh',*args],cwd=ROOT,text=True,timeout=120)
def decision(record,releases):
    previous=[r for r in releases if r['tag_name'].startswith('firmware-v')]
    for r in previous:
        v=r['tag_name'].removeprefix('firmware-v');version(v)
        if version(v)>version(record['version']):raise ValueError('source version is older than an existing release')
    matches=[r for r in previous if r['tag_name']==record['tag']]
    require(len(matches)<=1,'duplicate release tag')
    return matches[0] if matches else None

def main(source):
    record=verify(source);repo=os.environ['GITHUB_REPOSITORY']
    repository=json.loads(gh('api',f'repos/{repo}'))
    require(os.environ.get('GITHUB_REF')==f'refs/heads/{repository["default_branch"]}' and repository['default_branch'] in ('main','master'),'publication requires current default main/master branch')
    require(os.environ.get('GITHUB_EVENT_NAME') in ('push','workflow_dispatch'),'publication requires push/manual event')
    releases=[r for page in json.loads(gh('api',f'repos/{repo}/releases?per_page=100','--paginate','--slurp')) for r in page]
    existing=decision(record,releases)
    if existing:
        with tempfile.TemporaryDirectory() as temp:
            gh('release','download',record['tag'],'--repo',repo,'--dir',temp)
            prior=json.loads(file_bytes(Path(temp)/'release.json'))
            require(prior['source_fingerprint']==record['source_fingerprint'],'existing version contains different source; increment firmware version')
            if not existing['draft']:
                # Already published immutable version: verify its own full asset inventory/hash record.
                from release_assets import sha
                require(set(p.name for p in Path(temp).iterdir())==set(prior['assets'])|{'release.json','SHA256SUMS'},'existing release incomplete/unexpected assets')
                for name,meta in prior['assets'].items():
                    data=file_bytes(Path(temp)/name);require(meta=={'bytes':len(data),'sha256':sha(data)},'existing release asset corrupt')
                print('Version already published unchanged; no release modified:',record['tag']);return
            require(prior==record,'draft belongs to a different candidate; explicit review required')
            verify(source,Path(temp))
    else:
        notes=ROOT/'build/release-notes.md';notes.parent.mkdir(exist_ok=True)
        notes.write_text(f'RiscRTE {record["version"]} — {record["target"]}\n\nSource: `{source}`. Built and integration-tested in GitHub Actions.\n\nApp-only image offset: `0x10000`. Merged **new-deployment** image offset: `0x0`; includes partition table and bootfs, so never apply it over user storage without separate deployment approval. Debug ELF is not flashable. `default.elf` is the heartbeat module. See `release.json` and `SHA256SUMS` for exact bytes and provenance. No release publication flashes hardware.\n')
        gh('release','create',record['tag'],*map(str,sorted(OUTPUT.iterdir())),'--repo',repo,'--target',source,'--title',f'RiscRTE {record["version"]}','--notes-file',str(notes),'--draft')
        with tempfile.TemporaryDirectory() as temp:
            gh('release','download',record['tag'],'--repo',repo,'--dir',temp)
            verify(source,Path(temp))
    # Last mutation: expose the complete verified draft; never use --clobber.
    gh('release','edit',record['tag'],'--repo',repo,'--draft=false')
    published=json.loads(gh('api',f'repos/{repo}/releases/tags/{record["tag"]}'))
    require(not published['draft'],'GitHub did not confirm publication')
    print('Published:',published['html_url'])
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source-sha',required=True);a=p.parse_args();main(a.source_sha)
