#include "ports/esp32s3/CpuPort.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <string>
#include <vector>

// Production Runtime/Graph/CpuPort and actual mapped applications/providers.
// The prior characterization commit proves the 0.1.50 failures separately.
static_assert(offsetof(risc_runtime_api_v1, retain_invocation) == RISC_RUNTIME_BOOT_CONFIRM_V1_SIZE,
              "The complete legacy prefix must remain byte-for-byte intact");
static std::vector<std::string> events;
static std::string mode;
static unsigned invocations, delays;
static bool owned=true;
static risc_runtime_api_v1 savedApi{};
static RiscCpu::Port* cpu;
static const void* appImage;
static const char* appAllocation;
static const void* leafImage;
static const void* rootImage;
extern "C" void provider_exit_event(const char* event) { events.emplace_back(event); }
extern "C" const char* provider_exit_mode() { return mode.c_str(); }
extern "C" void provider_exit_save_allocation(const char* p) { appAllocation=p; }
extern "C" void provider_exit_save_api(const risc_runtime_api_v1* api) { savedApi=*api; }
extern "C" void provider_exit_owner(bool owner) { owned=owner; }
extern "C" unsigned provider_exit_invocation() { return ++invocations; }
extern "C" void provider_exit_save_app(const void* image) { appImage = image; }
extern "C" void provider_exit_save_provider(const void* image, bool leaf) {
  (leaf ? leafImage : rootImage) = image;
}
static unsigned count(const char* event) { return std::count(events.begin(), events.end(), event); }
static size_t first(const char* event) {
  auto found = std::find(events.begin(), events.end(), event);
  assert(found != events.end()); return size_t(found - events.begin());
}
static bool mapped(const void* address) { Dl_info info{}; return address && dladdr(address, &info); }
static void write(const std::string& root, const char* path, const std::string& text) {
  std::ofstream(root + "/" + path) << text;
}
int main(int argc, char** argv) {
  assert(argc == 3); const std::string root = argv[1]; mode = argv[2];
  const bool cpuRetained = mode == "cpu-gpio-retained-eager";
  const bool eager = mode == "operation-retained-eager" || cpuRetained || mode == "signal-child";
  const bool failedStart = mode == "start-retained";
  const bool nativeRetained = mode == "native-retained";
  const bool releaseRetained = mode == "release-retained" || mode == "release-retry";
  const bool operationRetained = mode == "operation-retained-demand" || eager;
  const bool finiRelease = mode == "fini-release-retained";
  const bool signaled = mode.find("signal-") == 0 || operationRetained;
  const bool retained = failedStart || nativeRetained || releaseRetained || finiRelease || signaled;
  write(root, "board.json", R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
  write(root, "root.json", R"({"type":"driver","id":"root","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"root.elf","requires":[],"provides":[{"capability":"test.root","api":1}]})");
  write(root, "leaf.json", R"({"type":"driver","id":"leaf","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"leaf.elf","requires":[{"capability":"test.root","api":1}],"provides":[{"capability":"test.leaf","api":1}]})");
  write(root, "app.json", R"({"type":"application","id":"provider-exit","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.leaf","api":1}]})");
  write(root, "boot.json", std::string(R"({"board":"board.json","default_app":"default.elf","provider_activation":")") +
    (eager ? "eager" : "demand") + R"(","drivers":[{"manifest":"leaf.json"},{"manifest":"root.json"}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.leaf","api":1,"instance_id":0}]}]})");
  if (cpuRetained) {
    write(root, "board.json", R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[{"instance_id":7,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[6],"active_high":true,"pull_up":false,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})");
    write(root, "root.json", R"({"type":"driver","id":"root","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"root.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.gpio","api":1}],"provides":[{"capability":"test.root","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})");
    write(root, "boot.json", R"({"board":"board.json","default_app":"default.elf","provider_activation":"eager","drivers":[{"manifest":"leaf.json"},{"manifest":"root.json","instance_id":7}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.leaf","api":1,"instance_id":0}]}]})");
    RiscCpu::Hardware h{};
    h.owner=[](){return true;}; h.now=[]()->uint64_t{return 0;}; h.sleep=[](uint32_t){};
    h.gpioOpen=[](uint8_t,bool,bool,bool){return true;};
    h.gpioWrite=[](uint8_t pin,bool){assert(pin==6);provider_exit_event("cpu:write-false");return false;};
    h.gpioRead=[](uint8_t,bool*){return true;}; h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){return true;};
    h.gpioClose=[](uint8_t){provider_exit_event("cpu:close");return true;};
    h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){return true;};
    h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){return true;};
    h.i2cClose=[](uint8_t){return true;}; h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){return true;};
    h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){return true;};
    h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){return true;};
    h.spiEnd=[](uint8_t,uint8_t,uint32_t){return true;}; h.spiClose=[](uint8_t){return true;};
    cpu=new RiscCpu::Port(h);
  }
  RiscBoot::Port port{[](){return owned;}, [](risc_runtime_health_v1*){return true;},
    [](uint32_t){assert(!count("app:signaled"));++delays;}, [](const char* line){assert(strcmp(line,"forbidden"));return true;}};
  port.appExitSafe = [](){return mode != "native-retained" || !count("app:operation-false");};
  if (cpu) {
    port.bindPlatforms=[](RiscBoot::Runtime& r){return cpu->bind(r);};
    port.appExitSafe=[](){return cpu->appExitSafe();};
  }
  if (mode == "legacy-prefix") {
    std::ifstream source(root+"/legacy.elf",std::ios::binary);
    std::ofstream destination(root+"/default.elf",std::ios::binary);
    destination << source.rdbuf();
  }
  auto* runtime = new RiscBoot::Runtime(port);
  assert(!runtime->retainInvocation());
  assert(runtime->prepare(root.c_str()));
  assert(!runtime->retainInvocation());
  assert(runtime->run() == !retained);
  assert(runtime->retained() == retained && !risc_runtime_get_api(1));
  if (cpu) {
    assert(cpu->appExitSafe() && cpu->providerStorageSafe() && !cpu->quiescent());
    assert(count("cpu:write-false")==1 && !count("cpu:close"));
  }
  for (const auto& event : events) std::printf("%s\n", event.c_str());
  std::printf("%s: retained=%u app-unloads=%u child-entries=%u fini-skipped=%u leaf-quiesces=%u\n",
    mode.c_str(), runtime->retained(), count("app:unloaded"), count("child:entry"),
    count("app:fini-skipped"), count("leaf:quiesce"));
  std::fflush(stdout);
  if (failedStart || releaseRetained || finiRelease)
    assert(strstr(runtime->error(), "provider retention barrier") && strstr(runtime->error(), "leaf"));
  if (nativeRetained) assert(strstr(runtime->error(), "native retention barrier"));
  if (retained) {
    const bool child = mode == "signal-child";
    const bool fini = mode == "signal-fini";
    assert(!count("app:fini-skipped"));
    assert(count("app:unloaded") == unsigned(child));
    assert(count("child:entry") == unsigned(child));
    assert(count("app:fini") == unsigned(child));
    assert(count("app:fini-signal") == unsigned(fini));
    assert(count("app:fini-release") == unsigned(finiRelease));
    assert(count("app:signaled") == unsigned(signaled));
    assert(count("leaf:quiesce") == (failedStart ? 2u : (releaseRetained || finiRelease) ? 1u : 0u));
    assert(mapped(appImage) && appAllocation && !strcmp(appAllocation,"still retained"));
    const unsigned before = delays;
    assert(!savedApi.retain_invocation() && !savedApi.request_launch("child.elf") && !savedApi.diagnostic("forbidden"));
    savedApi.yield_ms(1); assert(delays == before);
  } else if (mode == "legacy-prefix") {
    assert(count("legacy:complete") == 1 && !count("child:entry"));
  } else {
    const bool clean = mode == "release-recovered" || mode == "foreign-denied";
    assert(count("app:fini-skipped") == unsigned(!clean));
    assert(count("app:unloaded") == 3 && count("child:entry") == 1);
    assert(!mapped(appImage));
    assert(count("leaf:quiesce") == (mode == "release-recovered" ? 2u : 1u));
    assert(first("leaf:stop") < first("child:entry"));
  }
  if (retained) {
    if(mode != "signal-no-grants") assert(mapped(leafImage) && mapped(rootImage));
    assert(!runtime->run());
    // A retained graph cannot safely be destroyed. Native owners retain it
    // until restart; skip process loader destructors to preserve that evidence.
    std::_Exit(0);
  }
  delete runtime;
}
