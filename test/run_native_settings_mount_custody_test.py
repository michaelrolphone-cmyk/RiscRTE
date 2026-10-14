#!/usr/bin/env python3
"""Pinned mount + registered native workers, two concrete inspectors and stores.

--mount-root accepts the independently developed mount owner checkout; omit it
once the default-off mount owner and worker branches are integrated.
"""
import argparse, hashlib, importlib.util, json, os, pathlib, shutil, subprocess, tempfile
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--mount-root',type=pathlib.Path,default=root)
p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args();mount=a.mount_root.resolve();build=a.output.resolve();build.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('profile',root/'test/run_native_littlefs_profile_test.py')
profile=importlib.util.module_from_spec(spec);spec.loader.exec_module(profile)
vendor=build/'vendor';profile.obtain_inputs(vendor,os.environ.get('ESP_LITTLEFS_SOURCE'),os.environ.get('LITTLEFS_SOURCE'))
stubs=build/'stubs';shutil.copytree(root/'test/native_littlefs_profile_stubs',stubs,dirs_exist_ok=True)
for name in ('esp_vfs.h','esp_partition.h'):
 f=stubs/name;f.write_text('#ifdef __cplusplus\nextern "C" {\n#endif\n'+f.read_text()+'\n#ifdef __cplusplus\n}\n#endif\n')
s=(root/'test/native_littlefs_profile_fixture.c').read_text()
s=s.replace('static esp_vfs_t registered_vfs;','static const char* mount_path="/native";\nvoid native_lfs_fixture_select_appdata(void){mount_path="/appdata";}\nesp_err_t __real_esp_littlefs_format_partition(const esp_partition_t*);\nstatic esp_vfs_t registered_vfs;')
s=s.replace('strcmp(path, "/native")','strcmp(path, mount_path)').replace('registered_context ? "/native"','registered_context ? mount_path').replace('.base_path = "/native"','.base_path = mount_path')
s=s.replace('esp_littlefs_format_partition(&flash_partition)','__real_esp_littlefs_format_partition(&flash_partition)')
fixture=build/'fixture.c';fixture.write_text(s)
sanitizers=['-fsanitize='+os.environ.get('SANITIZERS','address,undefined'),'-fno-sanitize-recover=all','-fno-omit-frame-pointer','-g'] if os.environ.get('SANITIZE')=='1' else []
common=['-D_GNU_SOURCE','-DLFS_NO_DEBUG','-DLFS_NO_WARN','-DLFS_NO_ERROR','-pthread',*sanitizers,'-include',str(stubs/'sdkconfig.h')]
includes=['-I'+str(x) for x in (stubs,vendor,vendor/'littlefs',root,root/'src',root/'test',root/'sdk/app',root/'sdk/driver',root/'sdk/hardware',mount,mount/'src',mount/'src/ports/esp32s3')]
commands=[];inputs=[];objects=[]
def run(cmd):commands.append([str(x) for x in cmd]);profile.run(commands[-1])
for source in (vendor/'littlefs/lfs.c',vendor/'littlefs/lfs_util.c',vendor/'littlefs_esp_part.c',vendor/'esp_littlefs.c',fixture):
 obj=build/(source.name+'.o');run(['cc','-std=c11',*common,*includes,'-c',source,'-o',obj]);objects.append(obj)
flags=['-std=c++17',*common,'-I'+str(root/'test/native_settings_worker_shim'),*includes,'-DESP_PLATFORM','-DRISC_PAIRED_APP_DATA','-DRISC_NATIVE_APP_DATA_MOUNT=1','-DRISC_NATIVE_SETTINGS_WORKER=1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation']
for source in (mount/'src/ports/esp32s3/NativeAppDataMount.cpp',root/'src/ports/esp32s3/NativeSettingsWorker.cpp',root/'src/runtime/storage/AppDataFiles.cpp',mount/'test/native_app_data_mount_posix.cpp'):
 obj=build/(source.name+'.o');run(['c++',*flags,'-c',source,'-o',obj]);objects.append(obj);inputs.append(source)
source=root/'test/native_settings_mount_custody_test.cpp';inputs.append(source)
wrapped=['stat','fstat','open','read','write','close','fsync','mkdir','unlink','rename','opendir','readdir','closedir','esp_vfs_register','esp_vfs_unregister','esp_partition_read','esp_partition_write','esp_partition_erase_range','esp_littlefs_format','esp_littlefs_format_partition']
exe=build/'test';run(['c++',*flags,*(['-no-pie'] if sanitizers else []),source,*objects,'-Wl,'+','.join('--wrap='+f for f in wrapped),'-o',exe])
for case in ('healthy','retained','retained-close'):run([exe,case])
inputs.extend([mount/'src/ports/esp32s3/NativeAppDataMount.h',root/'src/ports/esp32s3/NativeSettingsWorker.h',root/'experimental/native_replica/NativeSettingsCoordinator.h',root/'experimental/native_replica/CheckedLittleFsFreshInitArtifacts.h'])
(build/'manifest.json').write_text(json.dumps(dict(commands=commands,sha256={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs},sanitizers=sanitizers,limits='Host RTOS/radio/metadata/flash models; actual selected mount and worker translation units, pinned VFS and LittleFS. No firmware activation or hardware claim.'),indent=2)+'\n')
