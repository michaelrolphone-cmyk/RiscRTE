"""Compile the production setup against native shims; optional platform failure stops boot."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHIM = r'''
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
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
using esp_err_t=int;constexpr esp_err_t ESP_OK=0;
struct risc_runtime_health_v1{uint32_t uptime_ms,free_heap,app_address;uint8_t mac[6];char target[64];};
#define RISC_BUILD_IDENTITY "RTE_SOURCE=test"
#define RISC_DIAGNOSTIC_ADAPTER 1
namespace RiscDiagnostics{inline void start(){mark("diagnostics");}inline void poll(){}inline void line(const char* s){mark(s);}}
namespace RiscPerf{inline void configure(uint64_t(*)(),bool(*)(),int){}inline void emit(unsigned,unsigned=0){}struct Scope{Scope(unsigned,unsigned){}};}
namespace RiscBoot{
struct Board{};class Runtime;
struct Port{bool(*owner)();bool(*health)(risc_runtime_health_v1*);void(*delay)(uint32_t);bool(*diagnostic)(const char*);bool(*bind)(Runtime&);void*kv;bool(*exitSafe)();bool(*storageSafe)();bool(*confirm)();void*data;void*wake;};
class Runtime{Board b;public:explicit Runtime(Port){}Board&board(){return b;}bool prepare(const char*){mark("prepare");return true;}bool run(){mark("run");return true;}const char*error(){return "test";}};
}
namespace RiscCpu{
inline int nativeHardware(bool(*)()){return 0;}
class Port{public:explicit Port(int){}bool bind(RiscBoot::Runtime&){return true;}bool appExitSafe(){return true;}bool providerStorageSafe(){return true;}bool restartResourcesSafe(){return true;}};
namespace NativeRetainedWake{inline void start(){mark("retained-wake");}inline void*backend(){return nullptr;}}
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
 std::vector<std::string> expected={"retained-wake","realtime","serial","diagnostics","RTE_SOURCE=test"};
#ifdef STARTUP_HOOK
 expected.push_back("startup-status");
#endif
#ifdef STARTUP_FAIL
 expected.push_back("RTE_BOOT error=native-startup detail=test-rail-hold");
#else
 for(const char* s:{"mount","reserve","prepare","RTE_BOOT board=validated drivers=admitted","run","RTE_BOOT state=idle reason=app-returned"})expected.emplace_back(s);
#endif
 assert(calls==expected);loop();assert(calls.back()=="yield");
}
'''


class NativeStartup(unittest.TestCase):
    def test_production_setup_optional_status(self):
        headers = ('Arduino.h', 'RiscBuildIdentity.h', 'esp_spiffs.h', 'esp_ota_ops.h',
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
            shutil.copyfile(ROOT/'src/main.cpp', folder/'main.cpp')
            (folder/'harness.cpp').write_text(HARNESS)
            for name, flags in (('no-hook', []), ('success', ['-DSTARTUP_HOOK']),
                                ('failure', ['-DSTARTUP_HOOK', '-DSTARTUP_FAIL'])):
                with self.subTest(name=name):
                    output = folder/name
                    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-unused-function', '-DRISC_EMBEDDED_BOOTSTORE=1', *flags, '-I'+str(folder),
                                    str(folder/'main.cpp'), str(folder/'harness.cpp'),
                                    '-o', str(output)], check=True)
                    subprocess.run([str(output)], check=True)


if __name__ == '__main__':
    unittest.main()
