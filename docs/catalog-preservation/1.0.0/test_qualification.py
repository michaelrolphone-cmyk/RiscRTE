#!/usr/bin/env python3
"""Preparation/checker unit tests; fabricated records are not product evidence."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zlib
import fixtures
from review_result import review,load


def synthetic():
    files=[dict(volume='test-volume',destination='/'+x['source'],bytes=x['bytes'],
                sha256=x['sha256'],operation='create') for x in fixtures.inventory()['files'] if x['role']=='payload']
    artifact=dict(id='test-image',source='firmware.bin',bytes=1,sha256='a'*64)
    m={'product':{'id':'test-product','hardware_profiles':['test-profile'],'hardware_revisions':['test-rev']},
       'catalog_release':{'release_id':'1','revision':1},
       'firmware':{'id':'test-final','source_commit':'d'*40,'artifacts':[artifact],
                   'install':{'method_id':'test-method','artifact_ids':['test-image'],
                              'erase_disclosure':'Synthetic preserved effects',
                              'volume_effects':[{'volume':'test-volume','effect':'preserved'}],
                              'settings_effect':'preserved'}},
       'payload':{'inventory_sha256':'b'*64,'files':files,
                  'totals':{'file_count':len(files),'bytes':sum(x['bytes'] for x in files)}},
       'plan':{'order':['firmware','payload','verify'],'pre_firmware_id':'test-old'}}
    raw=json.dumps(m,sort_keys=True).encode()
    r=fixtures.template()
    for k in ('run_id','started_at','finished_at','operator','device_identifier','host_os_browser'):
        r[k]='fabricated-checker-test'
    r.update(product='test-product',hardware_revision='test-rev',hardware_profile='test-profile',
             client_source_commit='e'*40,firmware_before='test-old',firmware_after='test-final',
             firmware_source_after='d'*40,release_id='1',catalog_revision=1,flash_method='test-method',
             erase_disclosure='Synthetic preserved effects',manifest_sha256=hashlib.sha256(raw).hexdigest(),
             container_sha256='c'*64,payload_inventory_sha256='b'*64,flash_order=m['plan']['order'],
             flash_artifact_hashes=[artifact],trusted_mapping_evidence=['synthetic-evidence'],
             admitted_namespaces_before=['synthetic'],admitted_namespaces_after=['synthetic'],
             provider_bounds={'synthetic':True},source_status='verified',software_tests_status='passed',
             publication_status='not_published',hardware_status='not_tested',
             evidence_class='software-injection',qualification_status='complete_evidence')
    r['sentinel_definitions']=[dict(id=k,kind=k,route='independent-native-owner',observer='synthetic',
                                    independent=True,flash_scope='settings' if k=='settings' else 'volume:test-volume')
                              for k in ('sibling','user-file','settings')]
    inventory=[dict(id=k,state={'persistent':True,'value':'non-default'} if k=='settings' else
                    {'exists':True,'type':'file','bytes':1,'sha256':'f'*64}) for k in ('sibling','user-file','settings')]
    r['snapshots']=[dict(id=k,stage=k,timestamp='synthetic',firmware_id='test-final',observer='independent',
                         environment='software-injection',evidence=['synthetic'],inventory=copy.deepcopy(inventory))
                    for k in ('S0','S1','S2','F0','F1')]
    for c in r['cases']:
        c.update(status='pass',environment='software-injection',evidence=['synthetic'],
                 serial_snapshots={'before':'S0','after':'S1'})
        for v in c['variants']:
            v.update(status='pass',environment='software-injection',evidence=['synthetic'],observation='Expected synthetic outcome')
    r['serial_preservation']=dict(status='pass',changed_unrelated=0,evidence=['synthetic-diff'])
    r['restart_persistence']=dict(status='pass',environment='software-injection',evidence=['synthetic'],
                                  before_snapshot='S1',after_snapshot='S2')
    r['target_consumer_usability']=dict(status='pass',environment='software-injection',evidence=['synthetic'])
    effects=[dict(scope='volume:test-volume',effect='preserved'),dict(scope='settings',effect='preserved')]
    r['flash_effects']=dict(declared=effects,observed=copy.deepcopy(effects),comparison='pass',
                            evidence=['synthetic'],environment='software-injection',before_snapshot='F0',after_snapshot='F1')
    r['file_results']=[dict(volume=x['volume'],destination=x['destination'],operation=x['operation'],
                            expected_bytes=x['bytes'],actual_bytes=x['bytes'],expected_sha256=x['sha256'],actual_sha256=x['sha256'],
                            commit_acknowledged=True,readback_close_acknowledged=True,readback_crc_match=True,
                            after_last_firmware_step=True,after_restart=True,consumer_usable=True,
                            environment='software-injection',status='verified',evidence=['synthetic'],
                            original_attempt_id='original-synthetic-attempt',recovery_receipt=None) for x in files]
    total=sum(x['bytes'] for x in files)
    r['final_payload']=dict(expected_files=len(files),verified_files=len(files),expected_bytes=total,
                            verified_bytes=total,status='verified',environment='software-injection')
    return r,m,raw


def hardware(r):
    r['evidence_class']='hardware'; r['hardware_status']='confirmed'
    for c in r['cases']:
        c['environment']='hardware'
        for v in c['variants']: v['environment']='hardware'
    for s in r['snapshots']: s['environment']='hardware'
    for f in r['file_results']: f['environment']='hardware'
    for k in ('restart_persistence','target_consumer_usability','flash_effects','final_payload'):
        r[k]['environment']='hardware'


class FixturesTest(unittest.TestCase):
    def test_metadata(self): fixtures.check()
    def test_boundaries(self):
        self.assertEqual([x['bytes'] for x in fixtures.inventory()['files'] if x['role']=='payload'],[0,1,47,48,49,257,4097])
        self.assertEqual(set(fixtures.content(257,0)),set(range(256)))
        self.assertEqual(fixtures.content(1,0),b'\0')
    def test_oracles(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'new'; fixtures.materialize(p)
            for x in fixtures.inventory()['files']:
                b=(p/x['source']).read_bytes()
                self.assertEqual(len(b),x['bytes'])
                self.assertEqual(hashlib.sha256(b).hexdigest(),x['sha256'])
                self.assertEqual(f'{zlib.crc32(b):08x}',x['crc32'])
            self.assertEqual(len(list(p.glob('*.bin'))),11)
    def test_no_overwrite(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d); (p/'empty.bin').write_bytes(b'preserve')
            with self.assertRaises(FileExistsError): fixtures.materialize(p)
            self.assertEqual((p/'empty.bin').read_bytes(),b'preserve')
    def test_template(self):
        r=fixtures.template(); self.assertEqual(len(r['cases']),46)
        self.assertEqual(r['hardware_status'],'not_tested'); self.assertEqual(r['qualification_status'],'unverified')
        for c in r['cases']:
            self.assertEqual(c['status'],'not_run')
            for v in c['variants']: self.assertEqual(v['status'],'not_run')


class ReportTest(unittest.TestCase):
    def setUp(self): self.r,self.m,self.raw=synthetic()
    def reject(self,part=None):
        errors=review(self.r,self.m,self.raw); self.assertTrue(errors)
        if part: self.assertTrue(any(part in x for x in errors),errors)
    def test_complete_software(self): self.assertEqual(review(self.r,self.m,self.raw),[])
    def test_complete_hardware(self):
        hardware(self.r); self.assertEqual(review(self.r,self.m,self.raw),[])
    def test_complete_mixed(self):
        hardware(self.r); self.r['evidence_class']='mixed'
        c=self.r['cases'][0]; c['environment']='software-injection'
        for v in c['variants']: v['environment']='software-injection'
        for sid in ('H0','H1'):
            s=copy.deepcopy(self.r['snapshots'][0]); s.update(id=sid,environment='software-injection'); self.r['snapshots'].append(s)
        c['serial_snapshots']={'before':'H0','after':'H1'}
        self.assertEqual(review(self.r,self.m,self.raw),[])
    def test_template_rejected(self): self.r=fixtures.template(); self.reject()
    def test_software_promotion_rejected(self):
        for cls in ('hardware','mixed','software-injection'):
            with self.subTest(cls=cls):
                self.r,self.m,self.raw=synthetic(); self.r.update(evidence_class=cls,hardware_status='confirmed'); self.reject()
    def test_unrelated_hardware_not_final_evidence(self):
        self.r['evidence_class']='mixed'; self.r['hardware_status']='confirmed'
        self.r['cases'][0]['variants'][0]['environment']='hardware'
        self.reject('physical final evidence required')
    def test_final_case_cannot_be_substitute(self):
        hardware(self.r); self.r['evidence_class']='mixed'
        c=next(x for x in self.r['cases'] if x['id']=='paired-final-usability')
        c['variants'][0]['environment']='software-injection'; self.reject('physical acceptance variant')
    def test_all_identity_bindings(self):
        for k,v in [('product','other'),('hardware_revision','other'),('hardware_profile','other'),('release_id','2'),
                    ('catalog_revision',2),('firmware_before','other'),('firmware_after','other'),
                    ('firmware_source_after','a'*40),('flash_method','other'),('erase_disclosure','other'),
                    ('payload_inventory_sha256','a'*64),('manifest_sha256','f'*64)]:
            with self.subTest(field=k):
                self.r,self.m,self.raw=synthetic(); self.r[k]=v; self.reject()
    def test_artifact_identity(self):
        for k,v in [('sha256','0'*64),('bytes',999),('source','other'),('id','other')]:
            with self.subTest(field=k):
                self.r,self.m,self.raw=synthetic(); self.r['flash_artifact_hashes']=copy.deepcopy(self.r['flash_artifact_hashes']); self.r['flash_artifact_hashes'][0][k]=v; self.reject('artifacts mismatch')
    def test_missing_duplicate_cases(self):
        self.r['cases'].pop(); self.reject('case coverage')
        self.r,self.m,self.raw=synthetic(); self.r['cases'].append(copy.deepcopy(self.r['cases'][0])); self.reject('duplicate')
    def test_failed_unverified_notrun_cases_and_variants(self):
        for status in ('failed','unverified','not_run'):
            for variant in (False,True):
                with self.subTest(status=status,variant=variant):
                    self.r,self.m,self.raw=synthetic(); c=self.r['cases'][0]; c=c['variants'][0] if variant else c; c['status']=status; self.reject()
    def test_missing_variant(self): self.r['cases'][0]['variants']=[]; self.reject('incomplete variants')
    def test_conditional_exclusion(self):
        c=next(x for x in self.r['cases'] if x['id']=='path-nested')
        c.update(status='not_applicable',not_applicable_reason='Flat provider',evidence=['profile'])
        self.assertEqual(review(self.r,self.m,self.raw),[])
    def test_required_exclusion_needs_harness(self):
        self.r['cases'][0].update(status='not_applicable',not_applicable_reason='Physical case unavailable',evidence=['profile'])
        self.reject('substitute')
    def test_required_substitute(self):
        c=self.r['cases'][0]; substitute=copy.deepcopy(c)
        c.update(status='not_applicable',not_applicable_reason='Physical case unavailable',evidence=['profile'],substitute_coverage=substitute)
        self.assertEqual(review(self.r,self.m,self.raw),[])
    def test_missing_independent_settings(self): self.r['sentinel_definitions'].pop(); self.reject('settings sentinels')
    def test_changed_serial_sentinel(self): self.r['snapshots'][1]['inventory'][0]['state']['sha256']='0'*64; self.reject('unrelated changes')
    def test_changed_restart_setting(self): self.r['snapshots'][2]['inventory'][2]['state']['value']='different'; self.reject('restart: unrelated changes')
    def test_flash_preserved_change(self): self.r['snapshots'][4]['inventory'][2]['state']['value']='erased'; self.reject('declared-preserved sentinel changed')
    def test_flash_effect_shapes_and_mismatch(self):
        for field,value in [('declared',['anything']),('observed',['anything']),('observed',[{'scope':'volume:test-volume','effect':'erased'},{'scope':'settings','effect':'preserved'}])]:
            with self.subTest(field=field,value=value):
                self.r,self.m,self.raw=synthetic(); self.r['flash_effects'][field]=value; self.reject()
    def test_missing_snapshot_or_independent_observer(self):
        self.r['cases'][0]['serial_snapshots']={}; self.r['sentinel_definitions'][0]['independent']=False; self.reject()
    def test_missing_extra_duplicate_files(self):
        for mutation in ('missing','extra','duplicate'):
            with self.subTest(mutation=mutation):
                self.r,self.m,self.raw=synthetic()
                if mutation=='missing': self.r['file_results'].pop()
                else:
                    f=copy.deepcopy(self.r['file_results'][0])
                    if mutation=='extra': f['destination']='/extra'
                    self.r['file_results'].append(f)
                self.reject()
    def test_file_oracles_and_status(self):
        for k,v in [('actual_bytes',999),('expected_bytes',999),('actual_sha256','a'*64),('operation','replace'),('status','unverified'),('actual_bytes',False)]:
            with self.subTest(field=k,value=v):
                self.r,self.m,self.raw=synthetic(); self.r['file_results'][0][k]=v; self.reject()
    def test_final_checks_required(self):
        for k in ('commit_acknowledged','readback_close_acknowledged','readback_crc_match','after_last_firmware_step','after_restart','consumer_usable'):
            with self.subTest(field=k):
                self.r,self.m,self.raw=synthetic(); self.r['file_results'][0][k]=False; self.reject()
    def test_ack_recovery_preserves_original(self):
        f=self.r['file_results'][0]; f['commit_acknowledged']=False
        f['recovery_receipt']=dict(original_status='unverified',original_receipt='immutable-original',run_id='recovery',
                                   explicit_recovery=True,safe_custody=True,fresh_session=True,independent_full_readback=True,evidence=['readback'])
        self.assertEqual(review(self.r,self.m,self.raw),[])
        f['recovery_receipt']['original_status']='verified'; self.reject('preserved original')
    def test_totals(self):
        for k in ('expected_files','verified_files','expected_bytes','verified_bytes'):
            with self.subTest(field=k):
                self.r,self.m,self.raw=synthetic(); self.r['final_payload'][k]=0; self.reject()
    def test_missing_evidence(self): self.r['cases'][0]['variants'][0]['evidence']=[]; self.reject()
    def test_nonfinite_anywhere(self):
        self.r['limitations']=[{'unrelated':float('nan')}]; self.reject('Non-finite')
    def test_json_parser(self):
        for value in ('{"a":1,"a":2}','{"nested":[NaN]}','{"nested":[Infinity]}','{"nested":[-Infinity]}','{"nested":[1e999]}'):
            with self.subTest(value=value),tempfile.TemporaryDirectory() as d:
                p=Path(d)/'bad.json'; p.write_text(value)
                with self.assertRaises(ValueError): load(p)


if __name__=='__main__': unittest.main(verbosity=2)
