import pathlib,json,shlex,hashlib,subprocess,zipfile
O=pathlib.Path(__file__).parent;R=pathlib.Path('/workspace/shared/serial-lifetime-20261018');W=pathlib.Path('/workspace/shared/wave-stat-successor');T=W/'toolchain/xtensa-esp-elf';C=T/'bin/xtensa-esp32s3-elf-g++'
def h(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def rec(p):return {'path':str(p),'bytes':p.stat().st_size,'sha256':h(p)}
d=json.loads((W/'public-native-qualification.json').read_text());z=zipfile.ZipFile(W/'wave060-first-native-evidence.zip')
for n in z.namelist():
 if 'locked-native/toolchain/' in n:
  p=O/'prior-evidence'/n.split('locked-native/')[1];p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(z.read(n))
cmds=json.loads((O/'prior-evidence/compile_commands.json').read_text());selected=next(r for r in cmds if r['file'].endswith('/src/main.cpp'));args=shlex.split(selected['command']);defs=[a for a in args if a.startswith('-D')];flags=[]
for a in args:
 if a.startswith(('-f','-W','-g','-O','-m','-std=')) and not a.startswith('-fmacro-prefix-map=') and a not in ['-fdiagnostics-color=always']:flags.append(a)
profile=W/'wave/profiles/native-idf61-settings.h'
sourceheaders=[R/n for n in ['src','include','sdk/app','sdk/driver','sdk/hardware','lib/ArduinoJson/src','lib/hal','lib/elf_loader/include']]
# Keep production definitions, code-generation flags, target response flags and profile. Host shims are a separately labelled fallback.
base=[str(C),*defs,*flags,'@'+str(O/'prior-evidence/toolchain/cxxflags'),'-include',str(profile),'-I'+str(O/'prior-evidence/config'),*['-I'+str(p) for p in sourceheaders]]
(O/'base-command.json').write_text(json.dumps(base,indent=2)+'\n')
known={}
for n,v in d['controlled_build']['tools'].items():
 if '/xtensa-esp-elf/' not in v['path']:continue
 local=T/v['path'].split('/xtensa-esp-elf/',2)[-1] if v['path'].count('/xtensa-esp-elf/')==1 else T/v['path'].split('/xtensa-esp-elf/')[-1]
 if local.exists():known[n]={**rec(local),'recorded_sha256':v['sha256'],'matches_recorded':h(local)==v['sha256']}
known['local_compiler']=rec(C)
prior={'archive':rec(W/'wave060-first-native-evidence.zip'),'qualification':rec(W/'public-native-qualification.json'),'compile_commands':rec(O/'prior-evidence/compile_commands.json'),'sdkconfig_header':rec(O/'prior-evidence/config/sdkconfig.h'),'profile_header':rec(profile),'tools':known,'missing_sdk':not pathlib.Path('/opt/esp/idf').exists(),'prior_does_not_qualify_current_source':True}
for n,k in [('compile_commands','compile_commands'),('sdkconfig_header','sdkconfig_header')]:prior[n]['expected_record']=d['controlled_build']['artifacts'][k];prior[n]['matches_recorded']=prior[n]['sha256']==prior[n]['expected_record']['sha256']
(O/'inputs.json').write_text(json.dumps(prior,indent=2)+'\n')
print(json.dumps({k:v for k,v in prior.items() if k not in ['tools']},indent=2));print('tool_hash_matches',[(n,v.get('matches_recorded')) for n,v in known.items()])
