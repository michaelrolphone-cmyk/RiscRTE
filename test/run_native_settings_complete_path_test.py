#!/usr/bin/env python3
"""Concrete Backend/Assembly/Worker/Lifecycle/Runtime/X4/FatFs/LittleFS qualification."""
import argparse,hashlib,importlib.util,json,os,pathlib,shutil,subprocess,shlex
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
for key in ['x4','drivers','reference-reader','sdk','vendor','initial-appdata','output']:p.add_argument('--'+key,type=lambda v:pathlib.Path(v).resolve(),required=True)
p.add_argument('--sanitize',action='store_true');p.add_argument('--build-only',action='store_true');a=p.parse_args();build=a.output;build.mkdir(parents=True,exist_ok=False)
assert a.initial_appdata.stat().st_size==0x80000 and hashlib.sha256(a.initial_appdata.read_bytes()).hexdigest()=='5f03c248f2de31c4da9ae8d9bc2033df064cee5e37694a9982f70fbb2f1d2ef0'
commands=[];campaigns=[]
def run(cmd):
 cmd=list(map(str,cmd));commands.append(cmd);print('+',shlex.join(cmd),flush=True);subprocess.run(cmd,check=True,timeout=180)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def tree(p):return {str(f.relative_to(p)):sha(f) for f in sorted(p.rglob('*')) if f.is_file() and '.git' not in f.parts and not any(x in f.parts for x in ['evidence','__pycache__']) and f.suffix in ['.h','.hpp','.cpp','.c','.inc','.py']}
inputs=tree(root)
source_status=subprocess.check_output(['git','-C',root,'status','--porcelain','--untracked-files=no'],text=True).strip()
toolchains={cc:subprocess.check_output([cc,'--version'],text=True).splitlines()[0] for cc in ['cc','c++']}
external=[]
for repo,local,published,paths in [(a.x4,'0a10bd15e3693768683fd41870f5d438ed57ccfe','3c32f85de9c3c80a6b085753d16744c47712172d',['minimal/drivers/x4pro_sd','minimal/test','minimal/interfaces','minimal/drivers/x4pro_board_power/PowerReadyV1.h']), (a.drivers,'ee244590d3c835af012776ce2480e47b25e23477','11bc45d263f830c70cea53a8c8749e4fcf17b041',['lib/StorageFatFs']), (a.reference_reader,'45cf61ac013fb618e8d7fed63217a35350484d52','45cf61ac013fb618e8d7fed63217a35350484d52',['test/storage_volume/fake','Drivers/x4pro_board'])]:
 records=[]
 for name in subprocess.check_output(['git','-C',repo,'ls-tree','-r','--name-only',local,'--',*paths],text=True).splitlines():
  data=(repo/name).read_bytes();assert data==subprocess.check_output(['git','-C',repo,'show',local+':'+name]),name;records.append(dict(path=name,sha256=sha(repo/name)))
 external.append(dict(root=str(repo),local_commit=local,published_commit=published,files=records))
spec=importlib.util.spec_from_file_location('profile',root/'test/run_native_littlefs_profile_test.py');profile=importlib.util.module_from_spec(spec);spec.loader.exec_module(profile)
vendor=build/'vendor';profile.obtain_inputs(vendor,str(a.vendor),str(a.vendor/'littlefs'))
stubs=build/'stubs';shutil.copytree(root/'test/native_littlefs_profile_stubs',stubs,dirs_exist_ok=True)
for name in ['esp_vfs.h','esp_partition.h']:
 q=stubs/name;q.write_text('#ifdef __cplusplus\nextern "C" {\n#endif\n'+q.read_text()+'\n#ifdef __cplusplus\n}\n#endif\n')
with (stubs/'esp_partition.h').open('a') as f:f.write('\ntypedef int esp_partition_subtype_t;\n')
(stubs/'freertos/task.h').write_text('''#pragma once
#include "FreeRTOS.h"
#include <stddef.h>
typedef unsigned char StackType_t;typedef void*TaskHandle_t;typedef struct {long storage[48];} StaticTask_t;
#ifdef __cplusplus
extern "C" {
#endif
static inline const char*pcTaskGetName(void*p){(void)p;return "host-task";}
void*xTaskGetCurrentTaskHandle(void);int xPortInIsrContext(void);void vTaskDelay(unsigned);
TaskHandle_t xTaskCreateStatic(void(*)(void*),const char*,uint32_t,void*,unsigned,StackType_t*,StaticTask_t*);
#define pdPASS 1
static inline int xTaskCreate(void(*entry)(void*),const char*n,unsigned s,void*p,unsigned pr,TaskHandle_t*out){(void)entry;(void)n;(void)s;(void)p;(void)pr;(void)out;return 0;}
#ifdef __cplusplus
}
#endif
''')
(stubs/'esp_heap_caps.h').write_text('#pragma once\n#include <stddef.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\n#define MALLOC_CAP_INTERNAL 4\nvoid*heap_caps_malloc(size_t,unsigned);void heap_caps_free(void*);\n')
(stubs/'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nstatic inline int64_t esp_timer_get_time(){return 1000;}\n')
(stubs/'soc').mkdir(exist_ok=True);(stubs/'soc/soc_memory_types.h').write_text('#pragma once\nbool esp_ptr_external_ram(const void*);bool esp_ptr_internal(const void*);\n')
(stubs/'mbedtls').mkdir(exist_ok=True);(stubs/'mbedtls/sha256.h').write_text('''#pragma once
#include <openssl/sha.h>
#include <string.h>
typedef SHA256_CTX mbedtls_sha256_context;
static inline void mbedtls_sha256_init(mbedtls_sha256_context*p){memset(p,0,sizeof *p);}
static inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context*p,int n){return n?-1:SHA256_Init(p)==1?0:-1;}
static inline int mbedtls_sha256_update_ret(mbedtls_sha256_context*p,const unsigned char*b,size_t n){return SHA256_Update(p,b,n)==1?0:-1;}
static inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context*p,unsigned char*b){return SHA256_Final(b,p)==1?0:-1;}
static inline void mbedtls_sha256_free(mbedtls_sha256_context*p){memset(p,0,sizeof *p);}
''')
(stubs/'bootloader_random.h').write_text('#pragma once\nextern "C" void bootloader_random_enable();extern "C" void bootloader_random_disable();\n');(stubs/'esp_random.h').write_text('#pragma once\n#include <stddef.h>\nextern "C" void esp_fill_random(void*,size_t);\n')
s=(root/'test/native_littlefs_profile_fixture.c').read_text().replace('static esp_vfs_t registered_vfs;','static const char*mount_path="/native";\nvoid native_lfs_fixture_select_appdata(void){mount_path="/appdata";}\nesp_err_t __real_esp_littlefs_format_partition(const esp_partition_t*);\nstatic esp_vfs_t registered_vfs;').replace('subtype != ESP_PARTITION_SUBTYPE_ANY','(subtype != ESP_PARTITION_SUBTYPE_ANY && subtype != 0x41)').replace('strcmp(path, "/native")','strcmp(path, mount_path)').replace('registered_context ? "/native"','registered_context ? mount_path').replace('.base_path = "/native"','.base_path = mount_path').replace('esp_littlefs_format_partition(&flash_partition)','__real_esp_littlefs_format_partition(&flash_partition)')
fixture=build/'fixture.c';fixture.write_text(s)
sdk=build/'sdk';shutil.copytree(a.sdk,sdk,dirs_exist_ok=True)
# Copy current versioned Runtime headers over the old prepared SDK snapshots.
for folder in ['sdk/app','sdk/driver','sdk/hardware']:
 for h in (root/folder).glob('*.h'):shutil.copy(h,sdk/h.name)
flags=['-g','-O0','-pthread','-fno-omit-frame-pointer','-D_GNU_SOURCE','-DLFS_NO_DEBUG','-DLFS_NO_WARN','-DLFS_NO_ERROR']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie']
incs=['-I'+str(x) for x in [stubs,vendor,vendor/'littlefs',root,root/'src',root/'test',root/'sdk/app',root/'sdk/driver',root/'sdk/hardware',root/'test/native_nvs_shim',sdk,a.reference_reader,a.reference_reader/'Drivers/x4pro_board',a.drivers/'lib/StorageFatFs',root/'test/drivers/stubs']]+['-isystem',str(root/'lib/ArduinoJson/src')]
selected=['-DBOARD_XTEINK_X4_PRO','-DRISC_PAIRED_BANKS','-DRISC_PAIRED_APP_DATA','-DRISC_NATIVE_SETTINGS_LIFECYCLE=1','-DRISC_NATIVE_SETTINGS_WORKER=1','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1','-DRISC_NATIVE_STORAGE_CUSTODY=1']
actual=selected+['-DESP_PLATFORM','-DRISC_NATIVE_APP_DATA_MOUNT=1','-DRISC_NATIVE_SETTINGS_BACKEND=1','-DRISC_NATIVE_SETTINGS_ASSEMBLY=1','-DRISC_NATIVE_SETTINGS_PSRAM_PAYLOAD=1','-DRISC_NATIVE_SETTINGS_STARTUP_ENTROPY=1']
objects=[]
for i,source in enumerate([vendor/'littlefs/lfs.c',vendor/'littlefs/lfs_util.c',vendor/'littlefs_esp_part.c',vendor/'esp_littlefs.c',fixture]):
 obj=build/f'c-{i}.o';run(['cc','-std=c11',*flags,*incs,'-include',stubs/'sdkconfig.h','-c',source,'-o',obj]);objects.append(obj)
sources=['src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp','src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp','src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp','src/ports/esp32s3/CpuPort.cpp','src/runtime/storage/AppDataFiles.cpp','src/ports/esp32s3/NativeSettingsMaintenance.cpp','src/ports/esp32s3/NativeSettingsBootstrap.cpp','test/native_settings_complete_posix.cpp']
for source in ['NativeAppDataMount','NativeSettingsBackend','NativeSettingsAssembly','NativeSettingsWorker','NativeSettingsLifecycle','NativeSettingsPayloadAllocator','NativeSettingsStartupEntropy']:sources.append('src/ports/esp32s3/'+source+'.cpp')
sources.append('test/native_settings_complete_path_test.cpp')
for i,name in enumerate(sources):
 source=root/name;obj=build/f'cpp-{i}.o';defines=actual if source.stem.startswith('NativeSettings') or source.stem in ['NativeAppDataMount','AppDataFiles','native_settings_complete_path_test'] else selected
 run(['c++','-std=c++17',*flags,*incs,*defines,'-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-Wno-missing-field-initializers','-Wno-deprecated-declarations','-MMD','-MF',build/f'cpp-{i}.d','-c',source,'-o',obj]);objects.append(obj)
layout=build/'layout.cpp';layout.write_text('#include "bootstrap/Runtime.h"\n#include "ports/esp32s3/CpuPort.h"\nextern "C" size_t complete_host_runtime_size(){return sizeof(RiscBoot::Runtime);}\nextern "C" size_t complete_host_port_size(){return sizeof(RiscCpu::Port);}\n');obj=build/'layout.o';run(['c++','-std=c++17',*flags,*incs,*selected,'-c',layout,'-o',obj]);objects.append(obj)
wrapped=['stat','fstat','open','read','write','close','fsync','mkdir','unlink','rename','opendir','readdir','closedir','esp_vfs_register','esp_vfs_unregister','esp_partition_read','esp_partition_write','esp_partition_erase_range','esp_littlefs_format','esp_littlefs_format_partition']
run(['c++',*flags,'-rdynamic','-no-pie',*objects,'-ldl','-lcrypto','-Wl,'+','.join('--wrap='+x for x in wrapped),'-o',build/'test'])
shared=[x for x in flags if x not in ['-fno-pie','-no-pie']]+['-fPIC','-shared']
run(['cc','-std=c11',*shared,*incs,'-DX4PRO_SD_ALLOW_LEGACY_GPIO=1','-DX4_SD_TEST_SOURCE="'+str(a.x4/'minimal/test/sd_test.c')+'"',root/'test/native_settings_complete_driver.c',a.drivers/'lib/StorageFatFs/fatfs/ff.c',a.drivers/'lib/StorageFatFs/fatfs/ffunicode.c','-o',build/'driver.elf'])
run(['cc','-std=c11',*shared,*incs,'-DX4_POWER_READY_HEADER="'+str(a.x4/'minimal/drivers/x4pro_board_power/PowerReadyV1.h')+'"',root/'test/native_sd_port_power_fixture.c','-o',build/'power.elf'])
fixture_root=a.sdk.parent
for name in ['board.json','power.json']:shutil.copy(fixture_root/name,build/name)
sd=json.loads((a.x4/'minimal/drivers/x4pro_sd/manifest.json').read_text());sd['requires'].append(dict(capability='platform.storage-worker',api=1));(build/'sd.json').write_text(json.dumps(sd))
apps=[]
for name,id,ns in [('settings','settings',1),('wifi','wifi_settings',6),('home','paper_clock',1),('battery','battery',1),('scanner','ble_scanner',1),('contexts','contexts',1)]:
 source=build/(name+'.c');source.write_text('extern void test_complete_app(unsigned); __attribute__((visibility("default"))) void app_main(void){test_complete_app('+str(ns)+');}\n');run(['cc','-std=c11',*shared,source,'-o',build/(name+'.elf')])
 (build/(name+'.json')).write_text(json.dumps(dict(type='application',id=id,version='1.0.0',architecture='xtensa-esp32s3',file_name=name+'.elf',entry='app_main',requires=[dict(capability='storage.key-value',api=1),dict(capability='storage.key-value.atomic',api=1),dict(capability='storage.key-value.availability',api=1)])))
 apps.append(dict(manifest=name+'.json',grants=[dict(capability='storage.key-value',api=1,instance_id=ns),dict(capability='storage.key-value.atomic',api=1,instance_id=ns),dict(capability='storage.key-value.availability',api=1,instance_id=ns)]))
(build/'boot.json').write_text(json.dumps(dict(board='board.json',default_app='settings.elf',provider_activation='demand',drivers=[dict(manifest='power.json',instance_id=1),dict(manifest='sd.json',instance_id=9)],installation=dict(volume_instance=9,grants=[]),app_capabilities=apps)))
if not a.build_only:
 for case in ['writer-identities','normal','restore','absent','ambiguous','conflict','interrupt','interruption-restore','first-cut','first-resume','no-entropy','publication-cut','publication-resume','publication-erase','async-restore']:
  out=build/case;out.mkdir(exist_ok=True);card=out/'card.img'
  if case in ['restore','conflict','interrupt','publication-cut','async-restore']:subprocess.run(['cp','--sparse=always',build/'normal/card.img',card],check=True)
  if case=='interruption-restore':subprocess.run(['cp','--sparse=always',build/'interrupt/card.img',card],check=True)
  if case in ['publication-resume','publication-erase']:subprocess.run(['cp','--sparse=always',build/'publication-cut/card.img',card],check=True)
  if case=='first-resume':subprocess.run(['cp','--sparse=always',build/'first-cut/card.img',card],check=True)
  cmd=[str(x) for x in [build/'test',build,case,card,out,a.initial_appdata]];commands.append(cmd)
  result=subprocess.run(cmd,timeout=180,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);print(result.stdout,end="",flush=True);campaigns.append(dict(case=case,exit_code=result.returncode,output=result.stdout,files={p.name:dict(sha256=sha(p),bytes=p.stat().st_size,allocated_bytes=p.stat().st_blocks*512) for p in out.iterdir() if p.is_file()}));assert result.returncode==({'interrupt':73,'first-cut':74,'publication-cut':75}.get(case,0)),case
  if case=='interrupt':assert sha(card)==sha(build/'normal/card.img')
  if case=='conflict':assert sha(card)==sha(build/'normal/card.img')
assert inputs==tree(root),'Source changed during build; rerun'
for group in external:
 for row in group['files']:assert sha(pathlib.Path(group['root'])/row['path'])==row['sha256'],'External source changed: '+row['path']
(build/'manifest.json').write_text(json.dumps(dict(commands=commands,campaigns=campaigns,inputs=inputs,source_status=source_status,toolchains=toolchains,external=external,littlefs=tree(vendor),sdk=tree(sdk),generated=tree(stubs),generated_fixtures={p.name:sha(p) for p in build.iterdir() if p.is_file() and p.suffix in ['.c','.cpp','.json']},sanitizer_environment={key:os.environ.get(key,'') for key in ['ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS']},sanitizers='address,undefined' if a.sanitize else 'none',binaries={p.name:sha(p) for p in build.iterdir() if p.suffix=='.elf' or p.name=='test'},initial_appdata=dict(path=str(a.initial_appdata),sha256=sha(a.initial_appdata)),runtime_commit=subprocess.check_output(['git','-C',root,'rev-parse','HEAD'],text=True).strip()),indent=2)+'\n')
