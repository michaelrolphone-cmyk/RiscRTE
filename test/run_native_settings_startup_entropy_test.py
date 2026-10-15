#!/usr/bin/env python3
"""Native startup ID derivation; modeled SDK entropy and owner, real host SHA256."""
import pathlib,os,subprocess,tempfile,hashlib,json
root=pathlib.Path(__file__).resolve().parents[1];out=pathlib.Path(os.environ.get('BUILD_DIR',tempfile.mkdtemp(prefix='settings-startup-entropy-')));out.mkdir(parents=True,exist_ok=True)
stubs=out/'stubs'
files={'bootloader_random.h':'#pragma once\nextern "C" void bootloader_random_enable();\nextern "C" void bootloader_random_disable();\n','esp_random.h':'#pragma once\n#include <stddef.h>\nextern "C" void esp_fill_random(void*,size_t);\n','freertos/FreeRTOS.h':'#pragma once\n','freertos/task.h':'#pragma once\nextern "C" int xPortInIsrContext();\n','soc/soc_memory_types.h':'#pragma once\nbool esp_ptr_internal(const void*);\nbool esp_ptr_in_dram(const void*);\n','mbedtls/sha256.h':'''#pragma once
#include <openssl/sha.h>
#include <cstring>
bool entropy_test_hash_fail();
typedef SHA256_CTX mbedtls_sha256_context;
inline void mbedtls_sha256_init(mbedtls_sha256_context*p){memset(p,0,sizeof *p);}
inline void mbedtls_sha256_free(mbedtls_sha256_context*p){memset(p,0,sizeof *p);}
inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context*p,int n){return entropy_test_hash_fail()||n?-1:SHA256_Init(p)==1?0:-1;}
inline int mbedtls_sha256_update_ret(mbedtls_sha256_context*p,const unsigned char*b,size_t n){return SHA256_Update(p,b,n)==1?0:-1;}
inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context*p,unsigned char*b){return SHA256_Final(b,p)==1?0:-1;}
'''}
for name,data in files.items():p=stubs/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(data)
flags=['-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-deprecated-declarations','-DESP_PLATFORM','-DBOARD_XTEINK_X4_PRO','-DRISC_NATIVE_SETTINGS_BACKEND=1','-DRISC_NATIVE_SETTINGS_STARTUP_ENTROPY=1','-I'+str(stubs),'-I'+str(root/'src')]
if os.environ.get('SANITIZE')=='1':flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-g']
cmd=['c++',*flags,str(root/'test/native_settings_startup_entropy_test.cpp'),'-lcrypto','-no-pie','-o',str(out/'test')];subprocess.run(cmd,check=True);subprocess.run([str(out/'test')],check=True)
sources=['src/ports/esp32s3/NativeSettingsStartupEntropy.h','src/ports/esp32s3/NativeSettingsStartupEntropy.cpp','test/native_settings_startup_entropy_test.cpp','test/run_native_settings_startup_entropy_test.py']
(out/'manifest.json').write_text(json.dumps(dict(command=cmd,source_sha256={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in sources},limitations=['RNG and RTOS modeled','Host OpenSSL implements SHA256','No physical entropy or firmware activation qualification']),indent=2)+'\n')
