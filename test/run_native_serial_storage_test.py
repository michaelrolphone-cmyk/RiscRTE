#!/usr/bin/env python3
"""Explicit serial product sessions through the actual pinned LittleFS/mount fence."""
import importlib.util
import os
from pathlib import Path
import shutil
import tempfile

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('profile',root/'test/run_native_littlefs_profile_test.py')
profile=importlib.util.module_from_spec(spec);spec.loader.exec_module(profile)
build=Path(os.environ.get('BUILD_DIR',tempfile.mkdtemp(prefix='native-serial-storage-')));build.mkdir(parents=True,exist_ok=True)
vendor=build/'vendor'
os.environ['RISC_LITTLEFS_NATIVE_IDF6']='1'
pin=root/'experimental/native_replica/pinned_littlefs/vendor'
profile.obtain_inputs(vendor,str(pin),str(pin/'littlefs'))
stubs=build/'stubs';shutil.copytree(root/'test/native_littlefs_profile_stubs',stubs,dirs_exist_ok=True)
for name in ('esp_vfs.h','esp_partition.h'):
    p=stubs/name;p.write_text('#ifdef __cplusplus\nextern "C" {\n#endif\n'+p.read_text()+'\n#ifdef __cplusplus\n}\n#endif\n')
with (stubs/'freertos/task.h').open('a') as stream:
    stream.write('\n#ifdef __cplusplus\nextern "C" {\n#endif\nvoid* xTaskGetCurrentTaskHandle(void);\nint xPortInIsrContext(void);\n#ifdef __cplusplus\n}\n#endif\n')
(stubs/'esp_memory_utils.h').write_text('#pragma once\nextern "C" bool native_mount_dram(const void*);\nstatic inline bool esp_ptr_in_dram(const void*p){return native_mount_dram(p);}\n')
(stubs/'esp_vfs_ops.h').write_text('#pragma once\n#include "esp_vfs.h"\ntypedef struct {int reserved;} esp_vfs_fs_ops_t;\n#ifdef __cplusplus\nextern "C" {\n#endif\nesp_err_t esp_vfs_register_fs(const char*,const esp_vfs_fs_ops_t*,int,void*);\nesp_err_t esp_vfs_unregister_fs(const char*);\n#ifdef __cplusplus\n}\n#endif\n')
s=(root/'test/native_littlefs_profile_fixture.c').read_text()
s=s.replace('static esp_vfs_t registered_vfs;','static const char* mount_path="/native";\nvoid native_lfs_fixture_select_appdata(void){mount_path="/appdata";}\nesp_err_t __real_esp_littlefs_format_partition(const esp_partition_t*);\nstatic esp_vfs_t registered_vfs;')
s=s.replace('subtype != ESP_PARTITION_SUBTYPE_ANY','(subtype != ESP_PARTITION_SUBTYPE_ANY && subtype != 0x41)')
s=s.replace('strcmp(path, "/native")','strcmp(path, mount_path)').replace('registered_context ? "/native"','registered_context ? mount_path').replace('.base_path = "/native"','.base_path = mount_path')
s=s.replace('esp_littlefs_format_partition(&flash_partition)','__real_esp_littlefs_format_partition(&flash_partition)')
s+='\n#include "esp_vfs_ops.h"\nesp_err_t esp_vfs_register_fs(const char*p,const esp_vfs_fs_ops_t*v,int f,void*c){(void)p;(void)v;(void)f;(void)c;return ESP_OK;}\nesp_err_t esp_vfs_unregister_fs(const char*p){(void)p;return ESP_OK;}\n'
s+='\noff_t __wrap_lseek(int fd,off_t at,int whence){return registered_vfs.lseek_p(registered_context,fd,at,whence);}\nint __wrap_ftruncate(int fd,off_t size){return registered_vfs.ftruncate_p(registered_context,fd,size);}\n'
fixture=build/'fixture.c';fixture.write_text(s)
sanitize=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie','-g'] if os.environ.get('SANITIZE')=='1' else []
common=['-D_GNU_SOURCE','-DLFS_NO_DEBUG','-DLFS_NO_WARN','-DLFS_NO_ERROR','-pthread',*sanitize,'-include',str(stubs/'sdkconfig.h')]
includes=['-I'+str(p) for p in (root/'test/native_diagnostic_workspace_shim',stubs,vendor,vendor/'littlefs',root,root/'src',root/'test',root/'sdk/app',root/'sdk/driver',root/'sdk/hardware')]
objects=[]
for source in (vendor/'littlefs/lfs.c',vendor/'littlefs/lfs_util.c',vendor/'littlefs_esp_part.c',vendor/'esp_littlefs.c',fixture):
    obj=build/(source.name+'.o');profile.run(['cc','-std=c11',*common,*includes,'-c',str(source),'-o',str(obj)]);objects.append(str(obj))
flags=['-std=c++17',*common,*includes,'-DESP_PLATFORM','-DRISC_PAIRED_APP_DATA','-DRISC_NATIVE_APP_DATA_MOUNT=1',
       '-DRISC_NATIVE_ESP_IDF=1','-DRISC_LITTLEFS_IDF6_MTIME32=1','-DRISC_NATIVE_SERIAL_FILES=1','-DRISC_NATIVE_SERIAL_APPDATA_FILES=1',
       '-Wall','-Wextra','-Werror','-Wno-misleading-indentation']
for name in ('src/ports/esp32s3/NativeAppDataMount.cpp','src/runtime/storage/ReopenedFiles.cpp','src/runtime/storage/AppDataFiles.cpp',
             'src/runtime/serial/Files.cpp','test/native_app_data_mount_posix.cpp'):
    source=root/name;obj=build/(source.stem+'.o');profile.run(['c++',*flags,'-c',str(source),'-o',str(obj)]);objects.append(str(obj))
wrapped=['stat','fstat','open','read','write','close','fsync','mkdir','unlink','rename','opendir','readdir','closedir','lseek','ftruncate',
         'esp_vfs_register','esp_vfs_unregister','esp_vfs_register_fs','esp_vfs_unregister_fs','esp_partition_read','esp_partition_write',
         'esp_partition_erase_range','esp_littlefs_format','esp_littlefs_format_partition']
for external in (0,1):
 for sd in (0,1):
    exe=build/('serial-'+str(external)+'-'+str(sd));profile.run(['c++',*flags,'-DRISC_NATIVE_SERIAL_SD='+str(sd),
      '-DCONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY='+str(external),'-DCONFIG_SPIRAM_BOOT_INIT='+str(external),
      str(root/'test/native_serial_storage_test.cpp'),*objects,'-Wl,'+','.join('--wrap='+f for f in wrapped),'-o',str(exe)])
    profile.run(['python3',str(root/'test/prove_native_serial_workspace.py'),str(exe),'--workspace','provider','--workspace','sessions','--external',str(external)])
    for case in ('unsafe-prepare','healthy','busy','capacity','first-full','first-busy','corrupt-root','directory-limit','close','sync','read','changed-size'):
        profile.run([str(exe),case])
print('Native serial private namespace: actual pinned LittleFS, both product selections PASS; hardware unqualified')
