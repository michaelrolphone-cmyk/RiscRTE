#!/usr/bin/env python3
"""Build real pinned LittleFS and native mtime port with a host flash model."""
import importlib.util,os,pathlib,subprocess,tempfile,shutil
root=pathlib.Path(__file__).resolve().parents[1]
pin=root/'experimental/native_replica/pinned_littlefs'
spec=importlib.util.spec_from_file_location('port',pin/'prepare_idf6.py');port=importlib.util.module_from_spec(spec);spec.loader.exec_module(port)
with tempfile.TemporaryDirectory(prefix='idf6-mtime-') as temp:
 build=pathlib.Path(temp);vendor=build/'vendor';port.prepare(vendor)
 stubs=root/'test/native_littlefs_profile_stubs'
 sanitize=['-fsanitize=undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'] if os.environ.get('SANITIZE')=='1' else []
 flags=['-D_GNU_SOURCE','-DLFS_NO_DEBUG','-DLFS_NO_WARN','-DLFS_NO_ERROR','-pthread',*sanitize,'-include',str(stubs/'sdkconfig.h')]+['-I'+str(p) for p in [stubs,vendor,vendor/'littlefs',root,root/'test']]
 fixture=build/'fixture.c';fixture.write_text((root/'test/native_littlefs_profile_fixture.c').read_text()+'\nint native_lfs_fixture_utime(const char*p,const struct utimbuf*t){return registered_vfs.utime_p(registered_context,p,t);}\n')
 objects=[]
 for source in [vendor/'littlefs/lfs.c',vendor/'littlefs/lfs_util.c',vendor/'littlefs_esp_part.c',vendor/'esp_littlefs.c',fixture]:
  obj=build/(source.name+'.o');subprocess.run(['cc','-std=c11',*flags,'-c',str(source),'-o',str(obj)],check=True);objects.append(str(obj))
 exe=build/'test';subprocess.run(['c++','-std=c++17',*flags,'-Wall','-Wextra','-Werror',str(root/'test/native_idf6_storage_mtime_test.cpp'),*objects,'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
