"""Compose actual pinned LittleFS without redirecting unrelated host file I/O."""
import importlib.util
import os
from pathlib import Path
import shutil

def build(root, output, run, sanitizer):
    spec=importlib.util.spec_from_file_location('native_lfs_profile',root/'test/run_native_littlefs_profile_test.py')
    profile=importlib.util.module_from_spec(spec);spec.loader.exec_module(profile)
    output.mkdir();vendor=output/'vendor';pin=root/'experimental/native_replica/pinned_littlefs/vendor'
    old=os.environ.get('RISC_LITTLEFS_NATIVE_IDF6');os.environ['RISC_LITTLEFS_NATIVE_IDF6']='1'
    profile.obtain_inputs(vendor,str(pin),str(pin/'littlefs'))
    if old is None:os.environ.pop('RISC_LITTLEFS_NATIVE_IDF6')
    else:os.environ['RISC_LITTLEFS_NATIVE_IDF6']=old
    stubs=output/'stubs';shutil.copytree(root/'test/native_littlefs_profile_stubs',stubs)
    for name in ('esp_vfs.h','esp_partition.h'):
        p=stubs/name;p.write_text('#ifdef __cplusplus\nextern "C" {\n#endif\n'+p.read_text()+'\n#ifdef __cplusplus\n}\n#endif\n')
    with (stubs/'freertos/task.h').open('a') as f:f.write('\n#ifdef __cplusplus\nextern "C" {\n#endif\nvoid* xTaskGetCurrentTaskHandle(void);\nint xPortInIsrContext(void);\n#ifdef __cplusplus\n}\n#endif\n')
    (stubs/'esp_memory_utils.h').write_text('#pragma once\nextern "C" bool native_mount_dram(const void*);\nstatic inline bool esp_ptr_in_dram(const void*p){return native_mount_dram(p);}\n')
    (stubs/'esp_vfs_ops.h').write_text('#pragma once\n#include "esp_vfs.h"\ntypedef struct {int reserved;} esp_vfs_fs_ops_t;\n#ifdef __cplusplus\nextern "C" {\n#endif\nesp_err_t esp_vfs_register_fs(const char*,const esp_vfs_fs_ops_t*,int,void*);\nesp_err_t esp_vfs_unregister_fs(const char*);\n#ifdef __cplusplus\n}\n#endif\n')
    s=(root/'test/native_littlefs_profile_fixture.c').read_text()
    s=s.replace('static esp_vfs_t registered_vfs;','static const char* mount_path="/native";\nvoid native_lfs_fixture_select_appdata(void){mount_path="/appdata";}\nesp_err_t __real_esp_littlefs_format_partition(const esp_partition_t*);\nstatic esp_vfs_t registered_vfs;')
    s=s.replace('subtype != ESP_PARTITION_SUBTYPE_ANY','(subtype != ESP_PARTITION_SUBTYPE_ANY && subtype != 0x41)')
    s=s.replace('strcmp(path, "/native")','strcmp(path, mount_path)').replace('registered_context ? "/native"','registered_context ? mount_path').replace('.base_path = "/native"','.base_path = mount_path')
    s=s.replace('esp_littlefs_format_partition(&flash_partition)','__real_esp_littlefs_format_partition(&flash_partition)')
    s+='\n#include "esp_vfs_ops.h"\nesp_err_t esp_vfs_register_fs(const char*p,const esp_vfs_fs_ops_t*v,int f,void*c){(void)p;(void)v;(void)f;(void)c;return ESP_OK;}\nesp_err_t esp_vfs_unregister_fs(const char*p){(void)p;return ESP_OK;}\n'
    s+='\noff_t serial_lfs_lseek(int fd,off_t at,int whence){return registered_vfs.lseek_p(registered_context,fd,at,whence);}\nint serial_lfs_ftruncate(int fd,off_t size){return registered_vfs.ftruncate_p(registered_context,fd,size);}\n'
    fixture=output/'fixture.c';fixture.write_text(s)
    posix=output/'posix.cpp';posix.write_text((root/'test/native_app_data_mount_posix.cpp').read_text().replace('__wrap_','serial_lfs_'))
    # Function-like macros preserve struct stat while redirecting only the real
    # serial private provider's calls. Runtime metadata and dlopen keep libc.
    names=('stat','fstat','open','read','write','close','fsync','mkdir','unlink','rename','opendir','readdir','closedir','lseek','ftruncate')
    redirect=output/'redirect.h';redirect.write_text('#include <cstdio>\n#include <unistd.h>\n#include <sys/stat.h>\n#include <fcntl.h>\n#include <dirent.h>\n'+''.join('extern \"C\" __typeof__(::'+n+') serial_lfs_'+n+';\n' for n in names)+'\n'.join('#define '+n+'(...) serial_lfs_'+n+'(__VA_ARGS__)' for n in names)+'\n')
    flags=['-D_GNU_SOURCE','-DLFS_NO_DEBUG','-DLFS_NO_WARN','-DLFS_NO_ERROR','-pthread','-g','-fno-omit-frame-pointer','-include',str(stubs/'sdkconfig.h')]
    if sanitizer!='none':flags+=['-fsanitize='+sanitizer,'-fno-sanitize-recover=all']
    includes=['-I'+str(p) for p in (root/'test/native_diagnostic_workspace_shim',stubs,vendor,vendor/'littlefs',root,root/'src',root/'test',root/'sdk/app',root/'sdk/driver',root/'sdk/hardware')]
    objects=[]
    for source in (vendor/'littlefs/lfs.c',vendor/'littlefs/lfs_util.c',vendor/'littlefs_esp_part.c',vendor/'esp_littlefs.c',fixture):
        obj=output/(source.name+'.o');run(['cc','-std=c11',*flags,*includes,'-c',source,'-o',obj]);objects.append(obj)
    selected=['-DRISC_PAIRED_APP_DATA','-DRISC_NATIVE_APP_DATA_MOUNT=1','-DRISC_NATIVE_ESP_IDF=1','-DRISC_LITTLEFS_IDF6_MTIME32=1','-DRISC_NATIVE_SERIAL_FILES=1','-DRISC_NATIVE_SERIAL_APPDATA_FILES=1','-DRISC_NATIVE_SERIAL_SD=1']
    for source in (root/'src/ports/esp32s3/NativeAppDataMount.cpp',root/'src/runtime/storage/ReopenedFiles.cpp',root/'src/ports/esp32s3/NativeSerialStorage.cpp',root/'test/native_serial_littlefs_composition.cpp',posix):
        obj=output/(source.name+'.o');extra=['-include',str(redirect)] if source.name=='ReopenedFiles.cpp' else []
        run(['c++','-std=c++17',*flags,*includes,*selected,'-DESP_PLATFORM',*extra,'-c',source,'-o',obj]);objects.append(obj)
    wrapped=('esp_vfs_register','esp_vfs_unregister','esp_vfs_register_fs','esp_vfs_unregister_fs','esp_partition_read','esp_partition_write','esp_partition_erase_range','esp_littlefs_format','esp_littlefs_format_partition')
    return objects,selected,['-Wl,'+','.join('--wrap='+f for f in wrapped)]
