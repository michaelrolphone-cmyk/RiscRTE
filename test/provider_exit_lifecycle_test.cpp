#include "ports/esp32s3/CpuPort.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <string>
#include <vector>

// Characterize exact Runtime ownership boundaries with real dlopen mappings.
// EXPECT_PROVIDER_EXIT_FENCED=1 checks the stronger desired terminal barrier;
// baseline mode records the 0.1.50 gaps without claiming that they are safe.
static std::vector<std::string> events;
static std::string mode;
static unsigned invocations;
static RiscCpu::Port* cpu;
static const void* appImage;
static const void* leafImage;
static const void* rootImage;
extern "C" void provider_exit_event(const char* event) { events.emplace_back(event); }
extern "C" const char* provider_exit_mode() { return mode.c_str(); }
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
  const bool eager = mode == "operation-retained-eager" || cpuRetained;
  const bool failedStart = mode == "start-retained";
  const bool nativeRetained = mode == "native-retained";
  const bool releaseRetained = mode == "release-retained";
  const bool operationRetained = mode == "operation-retained-demand" || eager;
  const bool retained = failedStart || nativeRetained || releaseRetained || operationRetained;
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
  RiscBoot::Port port{[](){return true;}, [](risc_runtime_health_v1*){return true;},
    [](uint32_t){}, [](const char*){return true;}};
  port.appExitSafe = [](){return mode != "native-retained" || !count("app:operation-false");};
  if (cpu) {
    port.bindPlatforms=[](RiscBoot::Runtime& r){return cpu->bind(r);};
    port.appExitSafe=[](){return cpu->appExitSafe();};
  }
  auto* runtime = new RiscBoot::Runtime(port);
  assert(runtime->prepare(root.c_str()));
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
  if (std::getenv("EXPECT_PROVIDER_EXIT_FENCED") && retained) {
    // An already-failed/retained provider must fence queued launches and pin the
    // current invocation, including after grantless failed acquisition.
    assert(!count("app:fini-skipped") && !count("app:unloaded") && !count("child:entry"));
    assert(mapped(appImage) && mapped(leafImage) && mapped(rootImage));
  } else if (nativeRetained) {
    assert(!count("app:fini-skipped") && !count("app:unloaded") && !count("child:entry"));
    assert(!count("leaf:quiesce") && mapped(appImage));
  } else if (releaseRetained || mode == "operation-retained-demand") {
    assert(count("app:fini-skipped") == 1 && !count("app:unloaded") && !count("child:entry"));
    assert(count("leaf:quiesce") == (releaseRetained ? 2u : 1u) && mapped(appImage));
  } else {
    assert(count("app:fini-skipped") == 1 && count("app:unloaded") == 3 && count("child:entry") == 1);
    if (eager) assert(first("app:unloaded") < first("leaf:quiesce"));
    assert(!mapped(appImage));
    if (failedStart) assert(count("leaf:quiesce") == 3);
    if (mode == "release-retry") assert(count("leaf:quiesce") == 2);
  }
  if (retained) {
    assert(mapped(leafImage) && mapped(rootImage));
    assert(!runtime->run());
    // A retained graph cannot safely be destroyed. Native owners retain it
    // until restart; skip process loader destructors to preserve that evidence.
    std::_Exit(0);
  }
  delete runtime;
}
