#!/usr/bin/env python3
"""Exercise NativeFileSource on the actual pinned VFS and shared mount fence."""
import os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parent.parent
mount=pathlib.Path(os.environ['MOUNT_ROOT']).resolve()
build=pathlib.Path(os.environ['MOUNT_BUILD']).resolve()
# First run MOUNT_ROOT/test/run_native_app_data_mount_test.py to materialize and
# qualify the pinned vendor/fixture objects. No remote download is done here.
stubs=build/'stubs';vendor=build/'vendor'
san=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-g'] if os.environ.get('SANITIZE')=='1' else []
flags=['-std=c++17','-D_GNU_SOURCE','-pthread',*san,'-include',str(stubs/'sdkconfig.h'),'-DESP_PLATFORM','-DRISC_PAIRED_APP_DATA=1','-DRISC_PAIRED_BANKS=1','-DRISC_NATIVE_APP_DATA_MOUNT=1','-DRISC_NATIVE_FILE_SOURCE=1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation']
flags+=['-I'+str(p) for p in [stubs,vendor,vendor/'littlefs',root/'src',root/'sdk/app',mount, mount/'src',mount/'test',mount/'src/ports/esp32s3']]
for source,obj in [(root/'src/ports/esp32s3/NativeFileSource.cpp',build/'file-source.o'),(mount/'src/ports/esp32s3/NativeAppDataMount.cpp',build/'source-mount.o')]:subprocess.run(['c++',*flags,'-c',str(source),'-o',str(obj)],check=True)
objects=[build/n for n in ['lfs.c.o','lfs_util.c.o','littlefs_esp_part.c.o','esp_littlefs.c.o','fixture.c.o','source-mount.o','file-source.o']]
wrap=['fstat','open','read','write','close','unlink','lseek','esp_vfs_register','esp_vfs_unregister','esp_partition_read','esp_partition_write','esp_partition_erase_range','esp_littlefs_format','esp_littlefs_format_partition']
exe=build/'file-source-test';subprocess.run(['c++',*flags,'-no-pie',str(root/'test/native_file_source_mount_test.cpp'),*map(str,objects),'-Wl,'+','.join('--wrap='+n for n in wrap),'-o',str(exe)],check=True)
for case in ['healthy','open-fault','close-fault','read-fault']:subprocess.run([str(exe),case],check=True)
