import pathlib,json,hashlib,re,datetime,subprocess
O=pathlib.Path(__file__).parent;P=O/'final';R=pathlib.Path('/workspace/shared/serial-lifetime-20261018');D=json.loads((P/'result.json').read_text());I=json.loads((O/'inputs.json').read_text());h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
profiles={}
for name in ['expanded','cohort','installed','legacy']:
 b0=D['types']['baseline-types-'+name+'-0'];c0=D['types']['candidate-types-'+name+'-0'];b1=D['types']['baseline-types-'+name+'-1'];c1=D['types']['candidate-types-'+name+'-1'];profiles[name]={'providers':c1['measure_Providers'],'grants':c1['measure_Grants'],'Runtime_serial_off':c0['measure_Runtime'],'Runtime_serial_on':c1['measure_Runtime'],'Runtime_change_serial_off':c0['measure_Runtime']-b0['measure_Runtime'],'Runtime_change_serial_on':c1['measure_Runtime']-b1['measure_Runtime'],'Graph':c1['measure_Graph']}
stack={}
for n,stem in [('expanded','shim-ProviderGraphV2-1'),('cohort','candidate-graph-cohort'),('installed','candidate-graph-installed'),('legacy','candidate-graph-legacy')]:
 line=next(s for s in (P/(stem+'.su')).read_text().splitlines() if 'validateKeepSet' in s);stack[n]={'own_frame_bytes':int(line.split('\t')[1]),'classification':line.split('\t')[2],'raw_source_record':line,'raw_array_bytes':profiles[n]['grants']+6*profiles[n]['providers']}
keys=['src/bootstrap/Runtime.cpp','src/bootstrap/Runtime.h','src/main.cpp','src/ports/esp32s3/NativeSerialFiles.cpp','src/ports/esp32s3/NativeSerialFiles.h','src/ports/esp32s3/NativeSerialStorage.cpp','src/ports/esp32s3/NativeSerialStorage.h','src/ports/esp32s3/NativeSerialStorageBinding.h','src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderGraphV2.h']
changed=[n for n,hash_ in D['after']['candidate'].items() if not (R/n).exists() or h(R/n)!=hash_]
receipt={'schema':1,'date_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'status':'scoped_target_object_and_layout_pass_full_sdk_link_blocked','source_root':str(R),'source_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=R,text=True).strip(),'source_hashes_stable_during_pass':D['source_hashes_stable'],'source_hashes_still_match_at_report':not changed,'files_changed_since_pass':changed,'source_hashes':{n:D['after']['candidate'][n] for n in keys},'verified_inputs':'inputs.json','raw_results':'final/result.json','commands':'final/commands.json','compiler':'Xtensa ESP GCC 15.2.0 esp-15.2.0_20251204','tool_hash_matches_prior_record':all(x.get('matches_recorded',True) for x in I['tools'].values()),'boundary_shim_compiles':{x['name']:x['returncode']==0 for x in D['compiles'] if x['boundary_shims']},'native_default_off_symbols_empty':all(not(P/('shim-'+n+'-0-nm.log')).read_text().strip() for n in ['NativeSerialFiles','NativeSerialStorage']),'profiles':profiles,'new_fields':{'NativeBindingLifetime_bytes':1,'NativeBindingLifetime_offset':25,'NativeFilesState_bytes':1,'NativeFilesBinding_before_and_after_bytes':36,'native_binding_array_before_and_after_bytes':576,'Port_before_bytes':152,'Port_after_bytes':156,'serialFilesStop_offset':152},'native_workspaces_before_and_after':D['types']['candidate-native-types'],'graph_validator_stack':stack,'stack_limitations':'Own static function frames, not complete call-chain bounds. Actual code calls memset, lease safety and stream safety callbacks; callback/library stack and target task stack adequacy are not established. No recursion in validator traversal.','placement':'Boundary attribute shim emits .ext_ram.bss.test; endpoint 2800, session 400, private provider 760 bytes; 24-byte serial control stays .bss. This proves compile selection and sizes only, not real SDK section placement.','full_link':I['full_link'],'profile_scope':'Archived expanded Waveshare profile definitions and exact profile header. Other variants change capacity branches only; they are not independently reconstructed product configurations or X4 hardware qualification.','qualification_limits':['No complete SDK-only feature-on TU build, no current firmware ELF/bin link, no current static IQ-bank non-overlap proof','No device access, target code execution, serial access, flashing, release or external publication','No physical heap/stack/PSRAM availability or hardware acceptance claims']}
(O/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
rows=[]
for n,v in profiles.items():rows.append(f"- {n}: {v['providers']} providers / {v['grants']} grants; Runtime off {v['Runtime_serial_off']:,}, on {v['Runtime_serial_on']:,} bytes; baseline delta 0/0.")
text='''# Serial lifetime: scoped Xtensa qualification

Result: production Runtime, main, NativeSerialFiles, NativeSerialStorage and ProviderGraphV2 compile with serial feature on and off using the verified Xtensa GCC 15.2 toolchain, archived flags/configuration/profile, and explicitly recorded boundary-header substitutions. All source hashes stayed stable during the final pass and still match this report. This is not a full SDK build or firmware qualification.

## Layout

'''+ '\n'.join(rows)+'''

These capacity variants retain the other archived expanded Waveshare definitions; they are not four reconstructed product profiles. NativeFilesBinding remains 36 bytes (16 slots, 576 bytes). The new 1-byte lifetime enum occupies offset 25, previously padding. The new Runtime state enum is 1 byte. Port grows 152 to 156 bytes with the final-stop function pointer at offset 152; complete Runtime size is nevertheless unchanged after alignment. Serial-off Runtime and Port sizes are unchanged.

Native endpoint workspace remains 2,800 bytes; serial State 24; sessions 400; private file provider 760. The off NativeSerialFiles and NativeSerialStorage objects have no symbols. The compile-only attribute shim puts workspaces into .ext_ram.bss.test and controls into .bss; no real SDK linked placement is inferred.

## Stack

The validator has a static own frame of 336 bytes at 40 providers / 56 grants, 272 bytes at 28/44 and 29/45, and 176 bytes at legacy 17/32. GCC .su output and Xtensa entry instructions are preserved. Its four scratch arrays total 296 bytes at the maximum (56 + 40 + 160 + 40). Traversal is bounded and nonrecursive. These figures exclude memset, safety hooks and other callees, so they are not whole-call-chain bounds or task-stack acceptance.

The expanded target reports nativeFilesServiceSafe 160 bytes, nativeFilesCheckpoint 32, stopNativeFiles 32, nativeFilesKeepSet 32, and releaseNativeFiles 48 bytes of own static frame.

## Verified limit

The complete pinned ESP-IDF checkout, static libraries and generated link inputs are absent. SDK-only compilation was attempted and failed on missing SDK headers; all failures are retained. Host-boundary declarations were then used solely for scoped target objects/layout. They include synthetic SHA operation, partition, NVS, heap declarations and a compile-only build identity; those SDK types/paths are not qualified by these objects. Full current-source firmware linking, real external-BSS placement and static IQ-bank non-overlap remain unproven. The older baseline ELF was hashed and identified as prior evidence only, never used as proof of this repair.

## Evidence

- receipt.json: concise machine-readable result and exact production source hashes
- inputs.json: actual compiler/tool hashes, verified archived compile database/configuration/profile hashes, SDK availability and substitutions
- final/commands.json: full commands, in single-job execution order
- final/result.json: all source hashes before/after, type symbols and compilation outcomes
- final/*.su and *-disassembly.log: actual Xtensa own-frame evidence
- final/*-nm.log, *-objdump.log, *.o: target symbols, sections and objects
- Earlier passes and failed probe logs are retained without modifying repository files.

No target instructions executed, device/serial access, flashing, merging, publication or hardware claim.
'''
(O/'README.md').write_text(text)
(O/'evidence-manifest.json').write_text(json.dumps({str(p.relative_to(O)):{'bytes':p.stat().st_size,'sha256':h(p)} for p in O.rglob('*') if p.is_file() and p.name!='evidence-manifest.json'},indent=2)+'\n')
print(json.dumps({'receipt':str(O/'receipt.json'),'source_hashes_stable':receipt['source_hashes_still_match_at_report'],'compile_pass':all(receipt['boundary_shim_compiles'].values()),'graph_stack':stack,'source_hashes':receipt['source_hashes']},indent=2))
