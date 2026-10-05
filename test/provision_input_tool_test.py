"""Offline artifacts only, with deliberately dummy network values."""
import csv,json,pathlib,subprocess,sys,tempfile
exe=sys.argv[1]
with tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp)
    profile={'schema':'riscrte.provisioning','schema_version':1,'wifi':{'ssid':'dummy-network','password':'dummy-password'},'files':[{'path':p,'url':'https://packages.example.invalid/'+p,'bytes':1,'sha256':'0'*64} for p in ('boot.json','board.json','default.elf')]}
    source=root/'input.json';raw=json.dumps(profile,indent=2).encode();source.write_bytes(raw)
    def run(name,ok=True,server=None):
        args=[exe,str(source),str(root/name)]+([server] if server else [])
        result=subprocess.run(args,capture_output=True)
        assert (result.returncode==0)==ok
        assert b'dummy-password' not in result.stdout+result.stderr
        return root/name
    out=run('bundle',server='time.example.invalid');assert (out.stat().st_mode&0o777)==0o700
    assert (out/'profile.bin').read_bytes()==raw
    descriptor=json.loads((out/'descriptor.bin').read_bytes());assert descriptor['profile_key']=='profile'
    rows=list(csv.DictReader((out/'nvs.csv').read_text().splitlines()))
    assert rows[0]=={'key':'rte_bootstrap','type':'namespace','encoding':'','value':''}
    for row in rows[1:]:
        assert row['type']=='data' and row['encoding']=='hex2bin'
        assert bytes.fromhex(row['value'])==(out/(row['key']+'.bin')).read_bytes()
    assert json.loads((out/'time.bin').read_bytes())['servers']==['time.example.invalid']
    assert (out/'COMPLETE').is_file()
    again=run('same',server='time.example.invalid')
    assert {p.name:p.read_bytes() for p in out.iterdir()}=={p.name:p.read_bytes() for p in again.iterdir()}
    run('bundle',False);assert (out/'profile.bin').read_bytes()==raw
    (root/'link').symlink_to(out,target_is_directory=True);run('link',False)
    run('bad-server',False,'https://wrong');assert not (root/'bad-server').exists()
    offline=run('offline');assert not (offline/'time.bin').exists()
    source.write_bytes(raw+b' '*(16384-len(raw)));assert (run('max')/'profile.bin').stat().st_size==16384
    for bad in (b'',b' '*16385,raw.replace(b'"schema_version": 1',b'"schema_version": 2'),raw.replace(b'"wifi": {',b'"unexpected": 1, "wifi": {')):
        source.write_bytes(bad);run('bad',False);assert not (root/'bad').exists()
    source.unlink();run('missing',False)
print('Owner input artifacts: deterministic, bounded, exact bytes, no overwrite, fixed diagnostics PASS')
