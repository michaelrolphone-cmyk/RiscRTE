"""Compile the production setup against native shims; optional platform failure stops boot."""
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHIM = r'''
#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "bootstrap/AppPolicyLimits.h"
#include "RiscResidentShellV1.h"
#include "runtime/drivers/SelectedNativeProviderPolicyV1.h"
#include "diagnostics/StageLog.h"
extern std::vector<std::string> calls;
inline void mark(const char* s){calls.emplace_back(s);}
using TaskHandle_t=void*;
inline TaskHandle_t xTaskGetCurrentTaskHandle(){return reinterpret_cast<void*>(1);}
inline uint32_t millis(){return 0;}
inline void vTaskDelay(uint32_t){mark("yield");}
constexpr uint32_t configTICK_RATE_HZ=1000;
struct SerialShim{void begin(unsigned){mark("serial");}};inline SerialShim Serial;
struct EspShim{uint32_t getFreeHeap(){return 0;}};inline EspShim ESP;
struct esp_partition_t{uint32_t address=0;};
inline const esp_partition_t* esp_ota_get_running_partition(){return nullptr;}
inline void esp_efuse_mac_get_default(uint8_t*){}
inline int64_t esp_timer_get_time(){return 0;}
enum {ESP_RST_PANIC=1,ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT,ESP_RST_BROWNOUT};
inline int esp_reset_reason(){return 0;}
using esp_err_t=int;constexpr esp_err_t ESP_OK=0;
struct esp_vfs_spiffs_conf_t{const char*base_path;const char*partition_label;size_t max_files;bool format_if_mount_failed;};
inline esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t*s){
 assert(s && !std::strcmp(s->base_path,"/bootfs") && !std::strcmp(s->partition_label,"bootfs"));
 assert(s->max_files==4 && !s->format_if_mount_failed);mark("mount");
#ifdef SPIFFS_FAIL
 return -1;
#else
 return ESP_OK;
#endif
}
namespace RiscNvs{inline esp_err_t initializationStatus(){return ESP_OK;}inline void*backend(){static int value;return &value;}}
struct risc_memory_heap_snapshot_v1{};
struct risc_runtime_health_v1{uint32_t uptime_ms,free_heap,app_address;uint8_t mac[6];char target[64];};
#define RISC_BUILD_IDENTITY "RTE_SOURCE=test"
#define RISC_DIAGNOSTIC_ADAPTER 1
namespace RiscDiagnostics{inline void start(){mark("diagnostics");}inline void poll(){}inline void line(const char* s){mark(s);}}
namespace RiscPerf{inline void configure(uint64_t(*)(),bool(*)(),int){}inline void emit(unsigned,unsigned=0){}struct Scope{Scope(unsigned,unsigned){}};}
namespace RiscBoot{
struct Board{};class Runtime;
struct Port{bool(*owner)();bool(*health)(risc_runtime_health_v1*);void(*delay)(uint32_t);bool(*diagnostic)(const char*);bool(*bind)(Runtime&);void*kv;bool(*exitSafe)();bool(*storageSafe)();bool(*confirm)();void*data;void*wake;void(*retainedDelay)(uint32_t);bool(*coldBoot)();bool(*priorFailure)(risc_resident_failure_v1*);RuntimeProviders::NativeProviderPolicySetV1 nativeProviders;void* failure;int32_t(*checkpoint)(const char*,uint64_t,const char*,uint32_t);bool(*ready)();bool(*begin)();void(*end)();bool(*monotonic)(uint32_t*);int32_t(*heap)(risc_memory_heap_snapshot_v1*);int32_t(*random)(void*,uint32_t);void(*controlBegin)(bool);bool(*controlEnd)();bool(*controlExitRequested)();};
class Runtime{Board b;public:explicit Runtime(Port p){assert(!p.nativeProviders.entries && !p.nativeProviders.count);
#ifdef RISC_EMBEDDED_BOOTSTORE
assert(!p.kv);
#else
assert(p.kv==RiscNvs::backend());
#endif
assert(p.controlBegin && p.controlEnd && p.controlExitRequested);}Board&board(){return b;}bool prepare(const char*){mark("prepare");return true;}bool run(){mark("run");return true;}const char*error(){return "test";}};
}
namespace RiscCpu{
namespace NativeTlsMemory{inline bool initialize(){mark("tls-allocator");
#ifdef TLS_ALLOCATOR_FAIL
return false;
#else
return true;
#endif
}}
namespace NativeFailureEvidence{inline void start(){mark("failure-evidence");}inline void*backend(){return nullptr;}}
inline bool nativeRadioResourceReady(){return true;}inline bool nativeRadioResourceTry(){return true;}inline void nativeRadioResourceEnd(){}
inline int32_t memoryHeap(risc_memory_heap_snapshot_v1*){return 0;}inline int32_t randomBytes(void*,uint32_t){return 0;}
inline RiscBoot::Runtime*createRetainedRuntime(RiscBoot::Port p){mark("metadata-psram");
#ifdef PSRAM_FAIL
 (void)p;return nullptr;
#else
 static RiscBoot::Runtime runtime(p);return &runtime;
#endif
}

inline int nativeHardware(bool(*)()){return 0;}
class Port{public:explicit Port(int){}bool bind(RiscBoot::Runtime&){return true;}bool appExitSafe(){return true;}bool providerStorageSafe(){return true;}bool restartResourcesSafe(){return true;}};
namespace NativeRetainedWake{inline void start(){mark("retained-wake");}inline void*backend(){return nullptr;}inline bool coldBoot(){return true;}}
namespace NativeRealtime{inline void start(){mark("realtime");}}
inline uint32_t cooperativeDelayTicks(uint32_t n,uint32_t){return n;}
inline void reserveNativePins(RiscBoot::Board&){mark("reserve");}
}
'''
HARNESS = r'''
#include "shim.h"
#include <cassert>
std::vector<std::string> calls;
esp_err_t riscrte_mount_embedded_store(){mark("mount");return ESP_OK;}
#ifdef STARTUP_HOOK
extern "C" const char* risc_native_startup_error(){mark("startup-status");
#ifdef STARTUP_FAIL
return "test-rail-hold";
#else
return nullptr;
#endif
}
#endif
extern void setup();extern void loop();
int main(){
 setup();
 std::vector<std::string> expected={"failure-evidence","retained-wake","realtime","serial","diagnostics","RTE_SOURCE=test",RISC_APP_POLICY_ROWS_MARKER};
#ifdef STARTUP_HOOK
 expected.push_back("startup-status");
#endif
#ifdef STARTUP_FAIL
 expected.push_back("RTE_BOOT error=native-startup detail=test-rail-hold");
#else
#ifdef RISC_ENABLE_HTTP
 expected.push_back("tls-allocator");
#ifdef TLS_ALLOCATOR_FAIL
 expected.push_back("RTE_HTTP unavailable=tls-psram-allocator");
#endif
#endif
 expected.push_back("mount");
#ifdef SPIFFS_FAIL
 expected.push_back("RTE_BOOT error=storage-mount code=-1");
#else
#ifdef RISC_RUNTIME_METADATA_PSRAM
 expected.push_back("metadata-psram");
#ifdef PSRAM_FAIL
 expected.push_back("RTE_BOOT error=runtime-metadata-psram");
#else
 for(const char* s:{"reserve","prepare","RTE_BOOT board=validated drivers=admitted","run","RTE_BOOT state=idle reason=app-returned"})expected.emplace_back(s);
#endif
#else
 for(const char* s:{"reserve","prepare","RTE_BOOT board=validated drivers=admitted","run","RTE_BOOT state=idle reason=app-returned"})expected.emplace_back(s);
#endif
 #endif
 #endif
 assert(calls==expected);loop();assert(calls.back()=="yield");
}
'''


class NativeStartup(unittest.TestCase):
    def test_production_setup_optional_status(self):
        headers = ('ports/esp32s3/NativeTlsMemory.h', 'esp_heap_caps.h', 'ports/esp32s3/NativeRadioResources.h',
                   'ports/esp32s3/NvsKeyValue.h',
                   'ports/esp32s3/NativeFailureEvidence.h', 'ports/esp32s3/NativeCompute.h',
                   'ports/esp32s3/NativeRuntime.h', 'ports/esp32s3/NativeBootstrap.h',
                   'ports/esp32s3/NativeSntp.h', 'ports/esp32s3/NvsBootstrapInput.h',
                   'ports/esp32s3/OwnerMaintenance.h', 'Arduino.h', 'RiscBuildIdentity.h', 'esp_spiffs.h', 'esp_ota_ops.h',
                   'esp_system.h', 'bootstrap/Runtime.h', 'ports/esp32s3/CpuPort.h',
                   'ports/esp32s3/NativeRetainedWake.h', 'ports/esp32s3/NativeRealtime.h',
                   'ports/esp32s3/NativeBoard.h', 'ports/esp32s3/CooperativeDelay.h',
                   'ports/esp32s3/SleepDiagnostics.h', 'esp_timer.h', 'diagnostics/Performance.h')
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            (folder/'shim.h').write_text(SHIM)
            for name in headers:
                path = folder/name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "shim.h"\n')
            # Use the production constants and disabled-log macro so this
            # fixture follows current startup declarations without replacing
            # or evaluating diagnostic arguments.
            for name in ('bootstrap/AppPolicyLimits.h', 'diagnostics/StageLog.h',
                         'ports/esp32s3/NativeDiagnosticBinding.h',
                         'ports/esp32s3/NativeCrownRecovery.h',
                         'runtime/drivers/NativeProviderPolicyV1.h',
                         'runtime/drivers/SelectedNativeProviderPolicyV1.h',
                         'runtime/packages/PackageIdentity.h'):
                (folder/name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT/'src'/name, folder/name)
            shutil.copyfile(ROOT/'sdk/app/RiscResidentShellV1.h',folder/'RiscResidentShellV1.h')
            source = (ROOT/'src/main.cpp').read_text()
            # Mach-O spells an optional undefined hook weak_import; ELF uses weak.
            if sys.platform == 'darwin':
                source = source.replace('__attribute__((weak))', '__attribute__((weak_import))')
            (folder/'main.cpp').write_text(source)
            (folder/'harness.cpp').write_text(HARNESS)
            for name, flags in (('no-hook', []), ('success', ['-DSTARTUP_HOOK']),
                                ('https', ['-DRISC_ENABLE_HTTP']),
                                ('https-fail', ['-DRISC_ENABLE_HTTP', '-DTLS_ALLOCATOR_FAIL']),
                                ('failure', ['-DSTARTUP_HOOK', '-DSTARTUP_FAIL']),
                                ('psram', ['-DRISC_RUNTIME_METADATA_PSRAM']),
                                ('psram-failure', ['-DRISC_RUNTIME_METADATA_PSRAM', '-DPSRAM_FAIL']),
                                ('spiffs', ['-DRISC_RUNTIME_METADATA_PSRAM']),
                                ('spiffs-psram-failure', ['-DRISC_RUNTIME_METADATA_PSRAM', '-DPSRAM_FAIL']),
                                ('spiffs-mount-failure', ['-DRISC_RUNTIME_METADATA_PSRAM', '-DSPIFFS_FAIL'])):
                with self.subTest(name=name):
                    output = folder/name
                    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-unused-function', *([] if name.startswith('spiffs') else ['-DRISC_EMBEDDED_BOOTSTORE=1']), *flags, '-I'+str(folder),
                                    str(folder/'main.cpp'), str(folder/'harness.cpp'),
                                    *(['-Wl,-U,_risc_native_startup_error'] if sys.platform == 'darwin' else []),
                                    '-o', str(output)], check=True)
                    subprocess.run([str(output)], check=True)


if __name__ == '__main__':
    unittest.main()
