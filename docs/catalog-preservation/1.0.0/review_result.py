#!/usr/bin/env python3
"""Check record consistency; observations and referenced evidence need independent review."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys

ROOT=Path(__file__).resolve().parent
SHA=re.compile(r'[0-9a-f]{64}\Z')
ENV={'software-injection','hardware'}


def string(x):
    return isinstance(x,str) and bool(x.strip())


def evidence(x):
    return isinstance(x,list) and bool(x) and all(string(v) for v in x)


def uint(x):
    return type(x) is int and x>=0


def digest(x):
    return isinstance(x,str) and bool(SHA.fullmatch(x))


def finite(x):
    if isinstance(x,float) and not math.isfinite(x):
        return False
    if isinstance(x,dict):
        return all(finite(v) for v in x.values())
    if isinstance(x,list):
        return all(finite(v) for v in x)
    return True


def review(r,m,raw):
    errors=[]
    def need(ok,msg):
        if not ok: errors.append(msg)
    def obj(x,label):
        if not isinstance(x,dict):
            errors.append(label+': expected object'); return {}
        return x
    def index(x,key,label):
        out={}
        if not isinstance(x,list):
            errors.append(label+': expected list'); return out
        for v in x:
            v=obj(v,label); k=key(v)
            valid=all(string(z) for z in k) if isinstance(k,tuple) else string(k)
            if not valid: errors.append(label+': missing key')
            elif k in out: errors.append(label+': duplicate '+str(k))
            else: out[k]=v
        return out
    r=obj(r,'report'); m=obj(m,'manifest')
    need(finite(r) and finite(m),'Non-finite JSON value')
    matrix=json.loads((ROOT/'matrix.json').read_text())
    need(r.get('version')=='1.0.0','Unknown report version')
    cls=r.get('evidence_class')
    need(cls in ('software-injection','hardware','mixed'),'Unknown evidence class')
    for k in ('run_id','started_at','finished_at','operator','product','hardware_revision',
              'hardware_profile','device_identifier','host_os_browser','client_source_commit',
              'firmware_before','firmware_after','firmware_source_after','release_id','flash_method','erase_disclosure'):
        need(string(r.get(k)),'Missing '+k)
    need(uint(r.get('catalog_revision')) and r['catalog_revision']>0,'Invalid catalog revision')
    for k in ('manifest_sha256','container_sha256','payload_inventory_sha256'):
        need(digest(r.get(k)),'Invalid '+k)
    need(r.get('manifest_sha256')==hashlib.sha256(raw).hexdigest(),'Exact manifest byte identity mismatch')
    payload=obj(m.get('payload'),'manifest payload')
    product=obj(m.get('product'),'manifest product')
    release=obj(m.get('catalog_release'),'manifest release')
    fw=obj(m.get('firmware'),'manifest firmware')
    install=obj(fw.get('install'),'manifest install')
    plan=obj(m.get('plan'),'manifest plan')
    need(r.get('product')==product.get('id'),'Product mismatch')
    need(r.get('hardware_revision') in product.get('hardware_revisions',[]),'Hardware revision mismatch')
    need(r.get('hardware_profile') in product.get('hardware_profiles',[]),'Hardware profile mismatch')
    need(r.get('release_id')==release.get('release_id') and r.get('catalog_revision')==release.get('revision'),'Release/catalog identity mismatch')
    need(r.get('firmware_after')==fw.get('id') and r.get('firmware_source_after')==fw.get('source_commit'),'Final firmware identity mismatch')
    if plan.get('pre_firmware_id') is not None:
        need(r.get('firmware_before')==plan['pre_firmware_id'],'Initial firmware identity mismatch')
    need(r.get('flash_method')==install.get('method_id'),'Flash method mismatch')
    need(r.get('flash_order')==plan.get('order'),'Flash order mismatch')
    need(r.get('erase_disclosure')==install.get('erase_disclosure'),'Erase disclosure mismatch')
    need(r.get('payload_inventory_sha256')==payload.get('inventory_sha256'),'Payload inventory mismatch')
    artifacts=index(fw.get('artifacts'),lambda x:x.get('id'),'manifest artifacts')
    selected=install.get('artifact_ids',[])
    need(isinstance(selected,list) and bool(selected) and len(set(selected))==len(selected),'Invalid selected artifact IDs')
    expected_artifacts={k:artifacts[k] for k in selected if k in artifacts}
    actual_artifacts=index(r.get('flash_artifact_hashes'),lambda x:x.get('id'),'flash artifacts')
    need(len(expected_artifacts)==len(selected) and actual_artifacts==expected_artifacts,'Selected firmware artifacts mismatch')
    for a in actual_artifacts.values():
        need(uint(a.get('bytes')) and digest(a.get('sha256')) and string(a.get('source')),'Invalid firmware artifact identity')
    need(evidence(r.get('trusted_mapping_evidence')),'Missing trusted mapping evidence')
    for k in ('admitted_namespaces_before','admitted_namespaces_after'):
        need(isinstance(r.get(k),list) and bool(r[k]),'Missing '+k)
    need(isinstance(r.get('provider_bounds'),dict) and bool(r['provider_bounds']),'Missing provider bounds')
    need(r.get('source_status')=='verified','Source not verified')
    need(r.get('software_tests_status')=='passed','Software tests incomplete')
    need(r.get('publication_status') in ('verified','not_published'),'Publication state unknown')
    need(r.get('qualification_status')=='complete_evidence','Qualification record incomplete')
    need(r.get('failures')==[],'Unresolved failures remain')
    need(isinstance(r.get('limitations'),list),'Limitations must remain explicit')

    environments=set()
    def environment(row,label):
        e=row.get('environment')
        need(e in ENV,label+': environment missing/unknown')
        if e in ENV: environments.add(e)
        return e
    defs=index(r.get('sentinel_definitions'),lambda x:x.get('id'),'sentinels')
    need({'sibling','user-file','settings'} <= {v.get('kind') for v in defs.values()},'Independent sibling/user-file/settings sentinels missing')
    for k,v in defs.items():
        need(string(v.get('observer')) and string(v.get('route')) and v.get('independent') is True,k+': independent observer missing')
        need(string(v.get('flash_scope')),k+': flash scope missing')
    snapshots=index(r.get('snapshots'),lambda x:x.get('id'),'snapshots')
    states={}
    for k,v in snapshots.items():
        environment(v,k)
        need(all(string(v.get(z)) for z in ('timestamp','observer','firmware_id','stage')) and evidence(v.get('evidence')),k+': snapshot provenance missing')
        items=index(v.get('inventory'),lambda x:x.get('id'),k+' inventory')
        need(set(items)==set(defs),k+': incomplete sentinel inventory')
        states[k]={}
        for sid,item in items.items():
            state=obj(item.get('state'),k+'/'+sid); states[k][sid]=state
            if defs.get(sid,{}).get('kind')=='settings':
                need('value' in state and state.get('persistent') is True,k+'/'+sid+': settings state missing')
            else:
                need(type(state.get('exists')) is bool,k+'/'+sid+': explicit existence missing')
                if state.get('exists'):
                    need(state.get('type') in ('file','directory'),k+'/'+sid+': type missing')
                    if state.get('type')=='file':
                        need(uint(state.get('bytes')) and digest(state.get('sha256')),k+'/'+sid+': file oracle missing')
    def snapshot_pair(row,label,serial=False):
        a,b=row.get('before'),row.get('after')
        valid=string(a) and string(b) and a!=b and a in states and b in states
        need(valid,label+': distinct snapshot pair missing')
        if valid and serial: need(states[a]==states[b],label+': unrelated changes')
        return (a,b) if valid else (None,None)

    cases=index(r.get('cases'),lambda x:x.get('id'),'cases')
    need(set(cases)=={c['id'] for c in matrix['cases']},'Incomplete/extra case coverage')
    effective={}
    for expected in matrix['cases']:
        cid=expected['id']; row=cases.get(cid,{})
        if row.get('status')=='not_applicable':
            need(string(row.get('not_applicable_reason')) and evidence(row.get('evidence')),cid+': unsupported exclusion')
            if expected['applicability']!='required': continue
            row=obj(row.get('substitute_coverage'),cid+' substitute')
            need(row.get('environment')=='software-injection',cid+': required harness substitute missing')
        effective[cid]=row
        need(row.get('status')=='pass' and evidence(row.get('evidence')),cid+': incomplete case')
        environment(row,cid)
        variants=index(row.get('variants'),lambda x:x.get('id'),cid+' variants')
        need(set(variants)==set(expected['variants']),cid+': incomplete variants')
        for vid,v in variants.items():
            environment(v,cid+'/'+vid)
            need(v.get('status')=='pass' and evidence(v.get('evidence')) and string(v.get('observation')),cid+'/'+vid+': incomplete observation')
        pair=obj(row.get('serial_snapshots'),cid+' serial snapshots')
        ca,cb=snapshot_pair(pair,cid,serial=True)
        if ca is not None:
            need(all(snapshots[s].get('environment')==row.get('environment') for s in (ca,cb)),cid+': snapshot environment mismatch')

    preservation=obj(r.get('serial_preservation'),'serial preservation')
    need(preservation.get('status')=='pass' and evidence(preservation.get('evidence')) and type(preservation.get('changed_unrelated')) is int and preservation['changed_unrelated']==0,'Serial preservation is not measured zero')
    restart=obj(r.get('restart_persistence'),'restart')
    consumer=obj(r.get('target_consumer_usability'),'consumer')
    for label,row in [('restart',restart),('consumer',consumer)]:
        environment(row,label)
        need(row.get('status')=='pass' and evidence(row.get('evidence')),label+': unverified')
    ra,rb=snapshot_pair({'before':restart.get('before_snapshot'),'after':restart.get('after_snapshot')},'restart',serial=True)
    flash=obj(r.get('flash_effects'),'flash effects'); environment(flash,'flash effects')
    need(flash.get('comparison')=='pass' and evidence(flash.get('evidence')),'Flash effects unverified')
    declared=index(flash.get('declared'),lambda x:x.get('scope'),'declared flash effects')
    observed=index(flash.get('observed'),lambda x:x.get('scope'),'observed flash effects')
    m_effects=index(install.get('volume_effects'),lambda x:x.get('volume'),'manifest effects')
    expected_effects={'volume:'+k:v.get('effect') for k,v in m_effects.items()}
    expected_effects['settings']=install.get('settings_effect')
    need({k:v.get('effect') for k,v in declared.items()}==expected_effects,'Declared flash effects mismatch')
    need(set(observed)==set(declared),'Incomplete observed flash effects')
    for k,v in declared.items():
        want=v.get('effect'); got=observed.get(k,{}).get('effect')
        need(want in ('preserved','erased','unknown') and got in ('preserved','erased','changed','unknown'),k+': invalid flash effect')
        if want!='unknown': need(got==want,k+': actual flash effect disagrees')
    fa,fb=snapshot_pair({'before':flash.get('before_snapshot'),'after':flash.get('after_snapshot')},'flash')
    if fa is not None:
        for sid,definition in defs.items():
            if declared.get(definition.get('flash_scope'),{}).get('effect')=='preserved':
                need(states[fa].get(sid)==states[fb].get(sid),sid+': declared-preserved sentinel changed')

    key=lambda x:(x.get('volume'),x.get('destination'))
    required=index(payload.get('files'),key,'manifest files')
    files=index(r.get('file_results'),key,'file results')
    need(bool(required) and set(files)==set(required),'Required-file coverage incomplete/extra')
    total=0
    for k,want in required.items():
        v=files.get(k,{}); label=str(k); environment(v,label)
        size,sha=want.get('bytes'),want.get('sha256')
        need(uint(size) and digest(sha),label+': invalid manifest file oracle')
        if uint(size): total+=size
        need(v.get('operation')==want.get('operation'),label+': operation changed')
        for f in ('expected_bytes','actual_bytes'):
            need(uint(v.get(f)) and v[f]==size,label+': '+f+' mismatch')
        for f in ('expected_sha256','actual_sha256'):
            need(v.get(f)==sha,label+': '+f+' mismatch')
        need(v.get('status')=='verified' and evidence(v.get('evidence')),label+': not verified')
        for f in ('readback_close_acknowledged','readback_crc_match','after_last_firmware_step','after_restart','consumer_usable'):
            need(v.get(f) is True,label+': '+f+' missing')
        need(string(v.get('original_attempt_id')),label+': original receipt missing')
        if v.get('commit_acknowledged') is not True:
            need(v.get('commit_acknowledged') is False,label+': acknowledgment state unknown')
            rec=obj(v.get('recovery_receipt'),label+' recovery')
            need(rec.get('original_status') in ('failed','unverified') and string(rec.get('original_receipt')) and string(rec.get('run_id')) and evidence(rec.get('evidence')),label+': preserved original/recovery missing')
            for f in ('explicit_recovery','safe_custody','fresh_session','independent_full_readback'):
                need(rec.get(f) is True,label+': recovery '+f+' missing')
    need(payload.get('totals')=={'file_count':len(required),'bytes':total},'Manifest totals inconsistent')
    final=obj(r.get('final_payload'),'final payload'); environment(final,'final payload')
    for f,n in [('expected_files',len(required)),('verified_files',len(required)),('expected_bytes',total),('verified_bytes',total)]:
        need(uint(final.get(f)) and final[f]==n,'Final '+f+' mismatch')
    need(final.get('status')=='verified','Final payload unverified')

    if cls=='software-injection':
        need(environments=={'software-injection'},'Software report contains other environments')
        need(r.get('hardware_status')=='not_tested','Software evidence cannot confirm hardware')
    elif cls in ('hardware','mixed'):
        need(environments==({'hardware'} if cls=='hardware' else ENV),'Evidence class contradicts environments')
        need(r.get('hardware_status')=='confirmed','Hardware observations unconfirmed')
        for label,row in [('restart',restart),('consumer',consumer),('final payload',final),('flash effects',flash)]:
            need(row.get('environment')=='hardware',label+': physical final evidence required')
        for k,v in files.items(): need(v.get('environment')=='hardware',str(k)+': physical readback required')
        for sid in (ra,rb,fa,fb):
            need(sid in snapshots and snapshots[sid].get('environment')=='hardware','Physical final snapshot required')
        for cid,variant in [('restart-persistence','default'),('paired-final-usability','supported-order')]:
            row=effective.get(cid,{})
            vs={v.get('id'):v for v in row.get('variants',[]) if isinstance(v,dict)}
            need(row.get('environment')=='hardware' and vs.get(variant,{}).get('environment')=='hardware',cid+': physical acceptance variant required')
    return errors


def no_duplicates(pairs):
    out={}
    for k,v in pairs:
        if k in out: raise ValueError('Duplicate JSON key: '+k)
        out[k]=v
    return out


def reject_constant(value):
    raise ValueError('Non-finite JSON: '+value)


def load(path):
    raw=Path(path).read_bytes()
    if len(raw)>16*1024*1024: raise ValueError('Record exceeds 16 MiB; reference evidence externally')
    obj=json.loads(raw,object_pairs_hook=no_duplicates,parse_constant=reject_constant)
    if not finite(obj): raise ValueError('Non-finite JSON')
    return obj,raw


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('report',type=Path); p.add_argument('--manifest',type=Path,required=True)
    a=p.parse_args()
    try:
        r,_=load(a.report); m,raw=load(a.manifest); problems=review(r,m,raw)
    except (ValueError,TypeError,KeyError,AttributeError,OSError) as e:
        problems=['Invalid/incomplete record: '+str(e)]
    if problems:
        print('INCOMPLETE EVIDENCE:\n'+'\n'.join('- '+x for x in problems)); sys.exit(1)
    print('COMPLETE EVIDENCE RECORD ('+r['evidence_class']+').')
    print('Consistency checked only; review actual artifacts and preserve product hardware gates.')
