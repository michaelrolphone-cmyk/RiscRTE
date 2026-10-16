#!/usr/bin/env python3
"""Deterministic qualification inputs and an unexecuted result template."""
import argparse
import hashlib
import json
from pathlib import Path
import zlib

ROOT = Path(__file__).resolve().parent
SPECS = [('empty',0,0,'payload'),('one',1,0,'payload'),
         ('boundary47',47,0,'payload'),('boundary48',48,0,'payload'),
         ('boundary49',49,0,'payload'),('binary257',257,0,'payload'),
         ('multichunk4097',4097,17,'payload'),('sibling',113,31,'sentinel'),
         ('directory-sibling',97,63,'sentinel'),('user-file',193,127,'sentinel'),
         ('other-namespace',71,191,'sentinel')]


def content(size, seed):
    return bytes((i * 73 + seed) % 256 for i in range(size))


def inventory():
    return {'version':'1.0.0', 'fixture_only':True,
            'algorithm':'byte[i] = (i * 73 + seed) modulo 256',
            'namespace_binding':None,
            'settings_sentinel':'Select a persistent non-default native setting and observe it through its independent owner. A serial file is not a settings sentinel.',
            'files':[dict(id=n, role=r, source=n+'.bin', bytes=b, seed=s,
                          sha256=hashlib.sha256(content(b,s)).hexdigest(),
                          crc32=f'{zlib.crc32(content(b,s)):08x}') for n,b,s,r in SPECS]}


def template():
    matrix = json.loads((ROOT/'matrix.json').read_text())
    r = {k:None for k in ['run_id','started_at','finished_at','operator','product',
        'hardware_revision','device_identifier','host_os_browser','client_source_commit',
        'firmware_before','firmware_after','release_id','catalog_revision',
        'manifest_sha256','container_sha256','payload_inventory_sha256','flash_method',
        'flash_order','erase_disclosure']}
    r.update(version='1.0.0', evidence_class=None, trusted_mapping_evidence=[],
             admitted_namespaces_before=[], admitted_namespaces_after=[],
             flash_artifact_hashes=[], provider_bounds={}, sentinel_definitions=[], snapshots=[])
    r['cases'] = [dict(id=c['id'],status='not_run',environment=None,evidence=[],
                      fixture=None,injection_point=None,observed_status=None,
                      last_acknowledged_offset=None,selected_file_state='unverified',
                      custody_state='unverified',session_usable=None,cleanup_sent=None,
                      automatic_retry=None,success_displayed=None,recovery_decision=None,
                      recovery_run_id=None,not_applicable_reason=None,substitute_coverage=None,
                      serial_snapshots={'before':None,'after':None},
                      variants=[dict(id=v,status='not_run',environment=None,evidence=[],observation=None)
                                for v in c['variants']]) for c in matrix['cases']]
    r.update(flash_effects={'declared':[],'observed':[],'comparison':'unverified','evidence':[],
                           'environment':None,'before_snapshot':None,'after_snapshot':None},
             file_results=[],serial_preservation={'changed_unrelated':None,'status':'unverified','evidence':[]},
             restart_persistence={'status':'unverified','environment':None,'evidence':[],
                                  'before_snapshot':None,'after_snapshot':None},
             target_consumer_usability={'status':'unverified','environment':None,'evidence':[]},
             final_payload={'expected_files':None,'verified_files':0,'expected_bytes':None,
                            'verified_bytes':0,'status':'unverified','environment':None},
             source_status='unverified',software_tests_status='not_run',publication_status='unverified',
             hardware_status='not_tested',qualification_status='unverified',failures=[],limitations=[])
    return r


def encoded(value):
    return json.dumps(value,indent=2,ensure_ascii=False)+'\n'


def check():
    for name,value in [('inventory.json',inventory()),('result-template.json',template())]:
        if (ROOT/name).read_text() != encoded(value):
            raise ValueError(name+' differs from deterministic definition')


def materialize(destination):
    destination=Path(destination)
    destination.mkdir(parents=True,exist_ok=False)
    for name,size,seed,_ in SPECS:
        with (destination/(name+'.bin')).open('xb') as f:
            f.write(content(size,seed))
    with (destination/'inventory.json').open('x') as f:
        f.write(encoded(inventory()))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    g=p.add_mutually_exclusive_group(required=True)
    g.add_argument('--check',action='store_true')
    g.add_argument('--write-metadata',action='store_true')
    g.add_argument('--materialize',type=Path)
    a=p.parse_args()
    if a.check:
        check(); print('Deterministic inventory and unexecuted template verified.')
    elif a.write_metadata:
        for name,value in [('inventory.json',inventory()),('result-template.json',template())]:
            (ROOT/name).write_text(encoded(value))
    else:
        check(); materialize(a.materialize); print('Fixture files created:',a.materialize)
