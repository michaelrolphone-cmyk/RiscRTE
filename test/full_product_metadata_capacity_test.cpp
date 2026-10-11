// Full X4 metadata graph admission, using production Runtime and CpuPort.
// Inert boundary adapted from the .50 packaging runtime_store_admission test.
// Inventory placeholders are deliberately NOT executable or ELF-qualified.
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/CpuPort.h"
#include <RiscDiagnosticSourceV1.h>
#include <RiscHttpClientV1.h>
#include <RiscBankStoreV1.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>

namespace {
unsigned hardwareCalls = 0, storageCalls = 0;
unsigned cooperativeYields = 0;
RiscCpu::Port* cpu = nullptr;
bool owner() { return true; }
risc_http_client_v1 httpApi{
  1,sizeof(httpApi),nullptr,
  [](void*,const risc_http_request_v1*,uint64_t*)->int32_t{++hardwareCalls;return RISC_HTTP_CLOSED;},
  [](void*,uint64_t,void*,uint32_t,uint32_t*)->int32_t{++hardwareCalls;return RISC_HTTP_CLOSED;},
  [](void*,uint64_t,risc_http_response_v1*)->int32_t{++hardwareCalls;return RISC_HTTP_CLOSED;},
  [](void*,uint64_t)->int32_t{++hardwareCalls;return RISC_HTTP_CLOSED;}
};
risc_bank_store_v1 bankApi{
  1,sizeof(bankApi),nullptr,
  [](void*,risc_bank_status_v1*){++hardwareCalls;return false;},
  [](void*,const risc_bank_image_v1*,uint64_t*)->int32_t{++hardwareCalls;return RISC_BANK_UNAVAILABLE;},
  [](void*,const char*,const void*,uint32_t,const risc_bank_image_v1*,uint64_t*)->int32_t{++hardwareCalls;return RISC_BANK_UNAVAILABLE;},
  [](void*,uint64_t,risc_bank_status_v1*)->int32_t{++hardwareCalls;return RISC_BANK_UNAVAILABLE;},
  [](void*,uint64_t,const void*,uint32_t)->int32_t{++hardwareCalls;return RISC_BANK_UNAVAILABLE;},
  [](void*,uint64_t)->int32_t{++hardwareCalls;return RISC_BANK_UNAVAILABLE;},
  [](void*,uint64_t)->int32_t{++hardwareCalls;return RISC_BANK_UNAVAILABLE;},
  [](void*,uint64_t)->int32_t{++hardwareCalls;return RISC_BANK_UNAVAILABLE;},
  [](void*,uint64_t){++hardwareCalls;return false;},
  [](void*,uint32_t,void*,uint32_t,uint32_t*)->int32_t{++hardwareCalls;return RISC_BANK_UNAVAILABLE;}
};
bool bind(RiscBoot::Runtime& runtime) {
  if (!cpu->bind(runtime)) return false;
  static const risc_diagnostic_source_api_v1 source{1,sizeof(source),nullptr,
    [](void*,uint32_t,char*,uint32_t,uint32_t*,uint64_t*,uint32_t*)->int32_t{
      ++hardwareCalls;return RISC_DIAGNOSTIC_SOURCE_INVALID;
    }};
  if(!runtime.registerPlatform(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,
       RiscBoot::Runtime::Scope::Global,0,&source))return false;

  if (!runtime.registerPlatform(RISC_HTTP_CLIENT_CAPABILITY,RISC_HTTP_CLIENT_API_V1,
                                RiscBoot::Runtime::Scope::Global,0,&httpApi)) return false;
  if (!runtime.registerPlatform(RISC_BANK_STORE_CAPABILITY,RISC_BANK_STORE_API_V1,
                                RiscBoot::Runtime::Scope::Global,0,&bankApi)) return false;
  return true;
}
RiscCpu::Hardware hardware() {
  RiscCpu::Hardware h{};
  h.usbPhyIdle=[](){++hardwareCalls;return false;};
  h.usbPhySuspend=[](){++hardwareCalls;return false;};
  h.usbPhyResume=[](){++hardwareCalls;return false;};
  h.hciOpen=[](){++hardwareCalls;return false;};
  h.hciSend=[](uint8_t,const uint8_t*,size_t,uint32_t){++hardwareCalls;return false;};
  h.hciReceive=[](uint8_t*,uint8_t*,size_t,size_t*,uint32_t){++hardwareCalls;return false;};
  h.hciClose=[](){++hardwareCalls;return false;};
  h.hciIdle=[](){++hardwareCalls;return false;};h.hciSafe=[](){++hardwareCalls;return false;};
  h.realtimeRead=[](risc_realtime_snapshot_v1*)->int32_t{++hardwareCalls;return RISC_REALTIME_IO;};
  h.realtimeSeed=[](int64_t,uint32_t)->int32_t{++hardwareCalls;return RISC_REALTIME_IO;};
  h.owner = owner;
  h.radioIqReady=[](){++hardwareCalls;return false;};
  h.radioIqPrepare=[](){++hardwareCalls;return false;};
  h.radioIqCleanup=[](){++hardwareCalls;return false;};
  h.now = []() -> uint64_t { ++hardwareCalls; return 0; };
  h.sleep = [](uint32_t) { ++hardwareCalls; };
  h.gpioOpen = [](uint8_t, bool, bool, bool) { ++hardwareCalls; return false; };
  h.gpioWrite = [](uint8_t, bool) { ++hardwareCalls; return false; };
  h.gpioRead = [](uint8_t, bool*) { ++hardwareCalls; return false; };
  h.gpioPwm = [](uint8_t, uint32_t, uint16_t, uint16_t) { ++hardwareCalls; return false; };
  h.gpioClose = [](uint8_t) { ++hardwareCalls; return false; };
  h.i2cOpen = [](uint8_t, uint8_t, uint8_t, uint32_t) { ++hardwareCalls; return false; };
  h.i2cTransfer = [](uint8_t, uint8_t, const uint8_t*, size_t, uint8_t*, size_t, uint32_t) { ++hardwareCalls; return false; };
  h.i2cClose = [](uint8_t) { ++hardwareCalls; return false; };
  h.spiOpen = [](uint8_t, int16_t, int16_t, int16_t) { ++hardwareCalls; return false; };
  h.spiBegin = [](uint8_t, uint8_t, uint32_t, uint8_t, uint32_t) { ++hardwareCalls; return false; };
  h.spiTransfer = [](uint8_t, const uint8_t*, uint8_t*, size_t, uint32_t) { ++hardwareCalls; return false; };
  h.spiEnd = [](uint8_t, uint8_t, uint32_t) { ++hardwareCalls; return false; };
  h.spiClose = [](uint8_t) { ++hardwareCalls; return false; };
  h.i2sOpen = [](uint8_t, uint8_t, uint8_t, uint8_t, uint32_t) { ++hardwareCalls; return false; };
  h.i2sWrite = [](uint8_t, const int16_t*, size_t, size_t*, uint32_t) { ++hardwareCalls; return false; };
  h.i2sOpenRx = [](uint8_t, uint8_t, uint8_t, uint32_t) { ++hardwareCalls; return false; };
  h.i2sRead = [](uint8_t, int16_t*, size_t, size_t*, uint32_t) { ++hardwareCalls; return false; };
  h.i2sClose = [](uint8_t) { ++hardwareCalls; return false; };
  h.radioJoin = [](const char*, const char*) { ++hardwareCalls; return false; };
  h.radioState = [](uint8_t*, int8_t*) { ++hardwareCalls; return false; };
  h.radioLeave = []() { ++hardwareCalls; return false; };
  h.radioAddresses = [](uint8_t*, uint8_t*) { ++hardwareCalls; return false; };
  h.radioScanStart = []() { ++hardwareCalls; return false; };
  h.radioScanPoll = [](garden_radio_scan_result_v1*) { ++hardwareCalls; return false; };
  h.radioScanCancel = []() { ++hardwareCalls; return false; };
  h.radioIdle = []() { ++hardwareCalls; return false; };
  return h;
}
}

namespace {
unsigned moduleCalls=0,bindCalls=0;
struct Inventory {unsigned apps=0,providers=0;};
constexpr const char* Placeholder="METADATA-ONLY: NOT AN EXECUTABLE OR FIRMWARE RECEIPT\n";
bool inventory(void* context,const char* path,bool provider){
  std::ifstream input(path,std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(input)),{});
  if(!input || bytes!=Placeholder)return false;
  auto& seen=*static_cast<Inventory*>(context);
  if(provider)++seen.providers;else ++seen.apps;
  return true;
}
RiscBoot::AppDataBackend appData(){
  return {nullptr,
    [](void*,uint32_t,const char*,uint32_t*,uint64_t*)->int32_t{++storageCalls;return RISC_APP_DATA_UNAVAILABLE;},
    [](void*,uint32_t,const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*)->int32_t{++storageCalls;return RISC_APP_DATA_UNAVAILABLE;},
    [](void*,uint32_t,const char*,uint64_t,const void*,uint32_t)->int32_t{++storageCalls;return RISC_APP_DATA_UNAVAILABLE;},
    [](void*){return true;}
  };
}
bool countedBind(RiscBoot::Runtime& runtime){++bindCalls;return bind(runtime);}
struct Context {
  RiscCpu::Port port{hardware()};
  const RiscBoot::KeyValueBackend kv{nullptr,
    [](void*,uint32_t,const char*,void*,uint32_t,uint32_t*)->int32_t{++storageCalls;return RISC_KEY_VALUE_IO;},
    [](void*,uint32_t,const char*,const void*,uint32_t)->int32_t{++storageCalls;return RISC_KEY_VALUE_IO;},
    RISC_KEY_VALUE_V2_BLOB_MAX};
  const RiscBoot::AppDataBackend files=appData();
  RiscRetainedWake::Image rtcImage{};
  RiscRetainedWake::Store wake{rtcImage};
  RiscBoot::Port callbacks(){
    RiscBoot::Port p{owner,[](risc_runtime_health_v1*){return true;},
      [](uint32_t){++cooperativeYields;},[](const char*){return true;},countedBind,&kv};
    p.appData=&files;p.retainedWake=&wake;
    p.coldBoot=[](){++hardwareCalls;return false;};
    return p;
  }
};
}
// Linker wrappers make any accidental app/provider execution a hard failure.
extern "C" void* __wrap_dlopen(const char*,int){++moduleCalls;std::abort();}
extern "C" void* __wrap_dlsym(void*,const char*){++moduleCalls;std::abort();}
extern "C" int __wrap_dlclose(void*){++moduleCalls;std::abort();}
int main(int argc,char** argv){
  assert(argc==3);
#ifndef FULL_PRODUCT_EXPECTED_PROVIDERS
#define FULL_PRODUCT_EXPECTED_PROVIDERS 26
#endif
  static_assert(FULL_PRODUCT_EXPECTED_PROVIDERS==24 || FULL_PRODUCT_EXPECTED_PROVIDERS==26,
                "Only original and expanded cohort metadata profiles are tested");
  static_assert(RiscLimits::Providers==FULL_PRODUCT_EXPECTED_PROVIDERS &&
                RiscLimits::Grants==FULL_PRODUCT_EXPECTED_PROVIDERS+16,
                "Compile against the requested original 24/40 or expanded 26/42 profile");
  static_assert(RiscBoot::Runtime::MaxAppPolicyGrants==17,
                "Preserve .50 Home's existing 17-row installed app policy");
  Context installedContext;cpu=&installedContext.port;
  auto installed=std::make_unique<RiscBoot::Runtime>(installedContext.callbacks());
  if(!installed->prepare(argv[1])){
    std::fprintf(stderr,"Baseline metadata rejected: %s\n",installed->error());return 2;
  }
  assert(installed->appCount()==21 && bindCalls==1);
  Inventory baseline;assert(installed->inspectImages(inventory,&baseline));
  assert(baseline.apps==21 && baseline.providers==23);
  Context directContext;cpu=&directContext.port;
  auto direct=std::make_unique<RiscBoot::Runtime>(directContext.callbacks());
  const bool prepared=direct->prepare(argv[2]);
  Inventory inspected;
  if(prepared)assert(direct->inspectImages(inventory,&inspected));
  const unsigned beforeCohortBind=bindCalls;
  auto candidate=std::make_unique<RiscBoot::Runtime>(RiscBoot::Port{});
  Inventory cohort;
  const bool admitted=installed->validateCohort(*candidate,argv[2],inventory,&cohort);
  assert(bindCalls==beforeCohortBind); // Staged graphs reuse scoped native tables.
  JsonDocument result;
  result["prepared"]=prepared;result["cohort_metadata_validated"]=admitted;
  result["prepare_error"]=std::string(direct->error());result["cohort_error"]=std::string(candidate->error());
  result["prepared_apps"]=inspected.apps;result["prepared_providers"]=inspected.providers;
  result["cohort_apps"]=cohort.apps;result["cohort_providers"]=cohort.providers;
  candidate.reset();direct.reset();installed.reset();
  assert(!hardwareCalls && !storageCalls && !moduleCalls);
  result["hardware_calls"]=hardwareCalls;result["storage_calls"]=storageCalls;
  result["module_calls"]=moduleCalls;result["cooperative_yields"]=cooperativeYields;
  result["cohort_rebound_platforms"]=bindCalls-beforeCohortBind;
  result["qualification"]="metadata only; inventory placeholders; no binary or hardware qualification";
  std::string output;serializeJson(result,output);std::puts(output.c_str());
}
