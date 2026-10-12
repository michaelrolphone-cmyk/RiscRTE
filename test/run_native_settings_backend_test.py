#!/usr/bin/env python3
"""Actual settings backend, fixed mount and source/store path; worker/factory/NVS host boundaries."""
import importlib.util, os, pathlib, subprocess, tempfile, shutil
root=pathlib.Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('profile',root/'test/run_native_littlefs_profile_test.py')
profile=importlib.util.module_from_spec(spec);spec.loader.exec_module(profile)
build=pathlib.Path(os.environ.get('BUILD_DIR',tempfile.mkdtemp(prefix='native-appdata-mount-')));build.mkdir(parents=True,exist_ok=True)
vendor=build/'vendor';profile.obtain_inputs(vendor,os.environ.get('ESP_LITTLEFS_SOURCE'),os.environ.get('LITTLEFS_SOURCE'))
stubs=build/'stubs';shutil.copytree(root/'test/native_littlefs_profile_stubs',stubs,dirs_exist_ok=True)
with (stubs/'freertos/task.h').open('a') as f:f.write('\n#ifdef __cplusplus\nextern "C" {\n#endif\nvoid* xTaskGetCurrentTaskHandle(void);\nint xPortInIsrContext(void);\n#ifdef __cplusplus\n}\n#endif\n')
for name in ['esp_vfs.h','esp_partition.h']:
 p=stubs/name;p.write_text('#ifdef __cplusplus\nextern "C" {\n#endif\n'+p.read_text()+'\n#ifdef __cplusplus\n}\n#endif\n')
with (stubs/'freertos/task.h').open('a') as f:f.write('\n#include <stddef.h>\ntypedef void* TaskHandle_t;\n#define pdPASS 1\nstatic inline void vTaskDelay(unsigned ticks){(void)ticks;}\nstatic inline int xTaskCreate(void(*entry)(void*),const char*name,unsigned stack,void*arg,unsigned priority,TaskHandle_t*out){(void)entry;(void)name;(void)stack;(void)arg;(void)priority;(void)out;return 0;}\n')
with (stubs/'esp_partition.h').open('a') as f:f.write('\ntypedef int esp_partition_subtype_t;\n')
(stubs/'esp_heap_caps.h').write_text('#pragma once\n#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\n#define MALLOC_CAP_INTERNAL 4\nstatic inline void* heap_caps_malloc(size_t n,unsigned caps){(void)caps;return malloc(n); }\n')
(stubs/'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nstatic inline int64_t esp_timer_get_time(){return 1000;}\n')
# Real payload adapter with tracked host PSRAM boundary and OpenSSL hash shim.
(stubs/'soc').mkdir(exist_ok=True)
(stubs/'soc/soc_memory_types.h').write_text('#pragma once\nbool esp_ptr_internal(const void*);\nbool esp_ptr_external_ram(const void*);\n')
(stubs/'esp_heap_caps.h').write_text('#pragma once\n#include <stddef.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\n#define MALLOC_CAP_INTERNAL 4\nvoid* heap_caps_malloc(size_t,unsigned);\nvoid heap_caps_free(void*);\n')
(stubs/'mbedtls').mkdir(exist_ok=True)
(stubs/'mbedtls/sha256.h').write_text('#pragma once\n#include <openssl/sha.h>\n#include <string.h>\ntypedef SHA256_CTX mbedtls_sha256_context;\nstatic inline void mbedtls_sha256_init(mbedtls_sha256_context*p){memset(p,0,sizeof *p);}\nstatic inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context*p,int n){return n?-1:SHA256_Init(p)==1?0:-1;}\nstatic inline int mbedtls_sha256_update_ret(mbedtls_sha256_context*p,const unsigned char*b,size_t n){return SHA256_Update(p,b,n)==1?0:-1;}\nstatic inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context*p,unsigned char*b){return SHA256_Final(b,p)==1?0:-1;}\n')
# Host esp_vfs.h declares the older ssize_t truncate member; IDF 4.4 uses int.

s= (root/'test/native_littlefs_profile_fixture.c').read_text()
s=s.replace('static esp_vfs_t registered_vfs;','static const char* mount_path="/native";\nvoid native_lfs_fixture_select_appdata(void){mount_path="/appdata";}\nesp_err_t __real_esp_littlefs_format_partition(const esp_partition_t*);\nstatic esp_vfs_t registered_vfs;')
s=s.replace('subtype != ESP_PARTITION_SUBTYPE_ANY','(subtype != ESP_PARTITION_SUBTYPE_ANY && subtype != 0x41)')
s=s.replace('strcmp(path, "/native")','strcmp(path, mount_path)').replace('registered_context ? "/native"','registered_context ? mount_path').replace('.base_path = "/native"','.base_path = mount_path')
s=s.replace('esp_littlefs_format_partition(&flash_partition)','__real_esp_littlefs_format_partition(&flash_partition)')
fixture=build/'fixture.c';fixture.write_text(s)
sanitize=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-g'] if os.environ.get('SANITIZE')=='1' else []
common=['-D_GNU_SOURCE','-DLFS_NO_DEBUG','-DLFS_NO_WARN','-DLFS_NO_ERROR','-pthread',*sanitize,'-include',str(stubs/'sdkconfig.h')]
includes=['-I'+str(p) for p in [stubs,vendor,vendor/'littlefs',root,root/'src',root/'test',root/'sdk/app',root/'sdk/driver',root/'sdk/hardware',root/'test/native_nvs_shim',root/'lib/ArduinoJson/src',pathlib.Path(os.environ['BACKEND_SDK'])]]
objects=[]
for source in [vendor/'littlefs/lfs.c',vendor/'littlefs/lfs_util.c',vendor/'littlefs_esp_part.c',vendor/'esp_littlefs.c',fixture]:
 obj=build/(source.name+'.o');profile.run(['cc','-std=c11',*common,*includes,'-c',str(source),'-o',str(obj)]);objects.append(str(obj))
flags=['-std=c++17',*common,*includes,'-DESP_PLATFORM','-DRISC_PAIRED_APP_DATA','-DRISC_PAIRED_BANKS','-DRISC_NATIVE_APP_DATA_MOUNT=1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-deprecated-declarations','-DBOARD_XTEINK_X4_PRO','-DRISC_NATIVE_SETTINGS_BACKEND=1','-DRISC_NATIVE_SETTINGS_ASSEMBLY=1','-DRISC_NATIVE_SETTINGS_PSRAM_PAYLOAD=1','-DRISC_NATIVE_SETTINGS_LIFECYCLE=1','-DRISC_NATIVE_SETTINGS_WORKER=1','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1','-DRISC_NATIVE_STORAGE_CUSTODY=1']
obj=build/'mount.o';profile.run(['c++',*flags,'-c',str(root/'src/ports/esp32s3/NativeAppDataMount.cpp'),'-o',str(obj)]);objects.append(str(obj))
for name in ('NativeSettingsBackend','NativeSettingsPayloadAllocator'):
 obj=build/(name+'.o');profile.run(['c++',*flags,'-c',str(root/'src/ports/esp32s3'/(name+'.cpp')),'-o',str(obj)]);objects.append(str(obj))
obj=build/'appdata.o';profile.run(['c++',*flags,'-c',str(root/'src/runtime/storage/AppDataFiles.cpp'),'-o',str(obj)]);objects.append(str(obj))
obj=build/'posix.o';profile.run(['c++',*flags,'-c',str(root/'test/native_app_data_mount_posix.cpp'),'-o',str(obj)]);objects.append(str(obj))
wrapped=['stat','fstat','open','read','write','close','fsync','mkdir','unlink','rename','opendir','readdir','closedir','esp_vfs_register','esp_vfs_unregister','esp_partition_read','esp_partition_write','esp_partition_erase_range','esp_littlefs_format','esp_littlefs_format_partition']
exe=build/'test';profile.run(['c++',*flags,str(root/'test/native_settings_backend_test.cpp'),*objects,'-Wl,'+','.join('--wrap='+f for f in wrapped),'-lcrypto','-no-pie','-o',str(exe)])
for case in ['normal','legacy','no-mount','wrong-owner','nvs-unavailable','busy-mount','wrong-worker','wrong-namespace','retained-flash']:profile.run([str(exe),case])
print('PASS: real native settings backend/VFS, source checks, namespace preservation and retained failure; no firmware activation/hardware claim')
