#!/usr/bin/env python3
"""Real native local-only assembly/lifecycle over LittleFS, without external media."""
import argparse, hashlib, importlib.util, json, os, pathlib, shutil, subprocess, shlex
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk',type=lambda v:pathlib.Path(v).resolve(),required=True)
p.add_argument('--output',type=lambda v:pathlib.Path(v).resolve(),required=True)
p.add_argument('--sanitize',action='store_true')
p.add_argument('--allow-fresh',action='store_true')
a=p.parse_args();build=a.output;build.mkdir(parents=True,exist_ok=False)
commands=[]
def run(cmd):
 cmd=list(map(str,cmd));commands.append(cmd);print('+',shlex.join(cmd),flush=True);subprocess.run(cmd,check=True,timeout=300)
spec=importlib.util.spec_from_file_location('profile',root/'test/run_native_littlefs_profile_test.py');profile=importlib.util.module_from_spec(spec);spec.loader.exec_module(profile)
vendor=build/'vendor';profile.obtain_inputs(vendor,os.environ.get('ESP_LITTLEFS_SOURCE'),os.environ.get('LITTLEFS_SOURCE'))
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
# Never replace divergent shared SDK headers.
for folder in ['sdk/app','sdk/driver','sdk/hardware']:
 for h in (root/folder).glob('*.h'):
  target=sdk/h.name
  if not target.is_file() or target.read_bytes()!=h.read_bytes():
   raise SystemExit('Prepared fixture SDK mismatch: '+h.name)
flags=['-g','-O0','-pthread','-fno-omit-frame-pointer','-D_GNU_SOURCE','-DLFS_NO_DEBUG','-DLFS_NO_WARN','-DLFS_NO_ERROR']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie']
incs=['-I'+str(x) for x in [stubs,vendor,vendor/'littlefs',root,root/'src',root/'test',root/'sdk/app',root/'sdk/driver',root/'sdk/hardware',root/'test/native_nvs_shim',sdk,root/'test/drivers/stubs']]+['-isystem',str(root/'lib/ArduinoJson/src')]
selected=['-DBOARD_XTEINK_X4_PRO','-DRISC_PAIRED_BANKS','-DRISC_PAIRED_APP_DATA','-DRISC_NATIVE_SETTINGS_LIFECYCLE=1','-DRISC_NATIVE_SETTINGS_WORKER=1','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1','-DRISC_NATIVE_STORAGE_CUSTODY=1','-DRISC_NATIVE_SETTINGS_NAMESPACE_MASK=2','-DRISC_NATIVE_SETTINGS_LOCAL_ONLY=1','-DRISC_NATIVE_SETTINGS_LOCAL_FRESH='+str(int(a.allow_fresh)), '-DRISC_NATIVE_SETTINGS_WRITERS={"wifi_settings",6}']
raw=(root/'test/native_settings_complete_path_test.cpp').read_text()
marker='int main(int argc,char**argv){'
assert raw.count(marker)==1
prefix=build/'local-port-fixture.inc';prefix.write_text(raw.split(marker)[0])
selected+=['-DNATIVE_LOCAL_PORT_FIXTURE="'+str(prefix)+'"']
actual=selected+['-DESP_PLATFORM','-DRISC_NATIVE_APP_DATA_MOUNT=1','-DRISC_NATIVE_SETTINGS_BACKEND=1','-DRISC_NATIVE_SETTINGS_ASSEMBLY=1','-DRISC_NATIVE_SETTINGS_PSRAM_PAYLOAD=1','-DRISC_NATIVE_SETTINGS_STARTUP_ENTROPY=1']
objects=[]
for i,source in enumerate([vendor/'littlefs/lfs.c',vendor/'littlefs/lfs_util.c',vendor/'littlefs_esp_part.c',vendor/'esp_littlefs.c',fixture]):
 obj=build/f'c-{i}.o';run(['cc','-std=c11',*flags,*incs,'-include',stubs/'sdkconfig.h','-c',source,'-o',obj]);objects.append(obj)
sources=['src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp','src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp','src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp','src/ports/esp32s3/CpuPort.cpp','src/runtime/storage/AppDataFiles.cpp','src/ports/esp32s3/NativeSettingsMaintenance.cpp','src/ports/esp32s3/NativeSettingsBootstrap.cpp','test/native_settings_complete_posix.cpp']
for source in ['NativeAppDataMount','NativeSettingsBackend','NativeSettingsAssembly','NativeSettingsWorker','NativeSettingsLifecycle','NativeSettingsPayloadAllocator','NativeSettingsStartupEntropy']:sources.append('src/ports/esp32s3/'+source+'.cpp')
sources.append('test/native_settings_local_only_port_test.cpp')
for i,name in enumerate(sources):
 source=root/name;obj=build/f'cpp-{i}.o';defines=actual if source.stem.startswith('NativeSettings') or source.stem in ['NativeAppDataMount','AppDataFiles','native_settings_local_only_port_test'] else selected
 instrument=['-finstrument-functions'] if source.stem in ['NativeSettingsBootstrap','NativeSettingsMaintenance','native_settings_local_only_port_test'] else []
 run(['c++','-std=c++17',*flags,*instrument,*incs,*defines,'-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-Wno-missing-field-initializers','-Wno-deprecated-declarations','-MMD','-MF',build/f'cpp-{i}.d','-c',source,'-o',obj]);objects.append(obj)
layout=build/'layout.cpp';layout.write_text('#include "bootstrap/Runtime.h"\n#include "ports/esp32s3/CpuPort.h"\nextern "C" size_t complete_host_runtime_size(){return sizeof(RiscBoot::Runtime);}\nextern "C" size_t complete_host_port_size(){return sizeof(RiscCpu::Port);}\n');obj=build/'layout.o';run(['c++','-std=c++17',*flags,*incs,*selected,'-c',layout,'-o',obj]);objects.append(obj)
wrapped=['stat','fstat','open','read','write','close','fsync','mkdir','unlink','rename','opendir','readdir','closedir','esp_vfs_register','esp_vfs_unregister','esp_partition_read','esp_partition_write','esp_partition_erase_range','esp_littlefs_format','esp_littlefs_format_partition']
run(['c++',*flags,'-rdynamic','-no-pie',*objects,'-ldl','-lcrypto','-Wl,'+','.join('--wrap='+x for x in wrapped),'-o',build/'test'])

shared=[x for x in flags if x not in ['-fno-pie','-no-pie']]+['-fPIC','-shared']
app=build/'wifi.c';app.write_text('void app_main(void){}\n')
run(['cc','-std=c11',*shared,app,'-o',build/'wifi.elf'])
(build/'board.json').write_text(json.dumps(dict(schema='riscrte.board-hardware',schema_version=1,board_id='local-only-host',revision='test',buses=[],devices=[])))
requires=[dict(capability=c,api=1) for c in ['storage.key-value','storage.key-value.atomic','storage.key-value.availability']]
(build/'wifi.json').write_text(json.dumps(dict(type='application',id='wifi_settings',version='1.0.0',architecture='xtensa-esp32s3',file_name='wifi.elf',entry='app_main',requires=requires)))
(build/'boot.json').write_text(json.dumps(dict(board='board.json',default_app='wifi.elf',provider_activation='demand',drivers=[],installation=dict(volume_instance=0,grants=[]),app_capabilities=[dict(manifest='wifi.json',grants=[dict(**v,instance_id=6) for v in requires])])))
probe=[str(build/'test'),str(build),'probe-external-acquire']
commands.append(probe)
negative=subprocess.run(probe,timeout=30)
assert negative.returncode==86,'External acquire instrumentation did not intercept real acquire'
cases=['fresh','legacy','cancel-inspect','cancel-init','cancel-committed']
for case in cases:run([build/'test',build,case])
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(build/'manifest.json').write_text(json.dumps(dict(cases=cases,commands=commands,local_fresh_permission=a.allow_fresh,source_hashes={str(p.relative_to(root)):sha(p) for directory in ['src','experimental/native_replica','test','sdk'] for p in (root/directory).rglob('*') if p.is_file() and p.suffix in ['.h','.cpp','.c','.py']},sdk_hashes={p.name:sha(p) for p in sdk.glob('*.h')},sanitizers='address,undefined' if a.sanitize else 'none',binary_sha256=sha(build/'test')),indent=2)+'\n')
