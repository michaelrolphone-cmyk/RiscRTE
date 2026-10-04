#include "ports/esp32s3/CpuPort.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

// Real Runtime/Graph/CPU + dlopen app/provider images. Only the lowest hardware
// and key-value backend are modeled. This does not simulate Xtensa allocation.
static std::string root, tracePath, mode;
static RiscCpu::Port* cpu;
static unsigned defaultRuns, delays, healthCalls, kvCalls;
static risc_runtime_api_v1 savedApi{};
static risc_runtime_capability_v1 savedGrant{};
static risc_key_value_v1 savedKeyValue{};
static const void* appImage;
static const char* appAllocation;

extern "C" void test_retained_trace(const char* text) {
  std::ofstream(tracePath, std::ios::app) << text << '\n';
}
extern "C" const char* test_retained_mode() { return mode.c_str(); }
extern "C" unsigned test_retained_default_run() { return ++defaultRuns; }
extern "C" void test_retained_save(const risc_runtime_api_v1* api,
                                  const risc_runtime_capability_v1* grant,
                                  const risc_key_value_v1* kv,
                                  const void* image, const char* allocation) {
  savedApi = *api;
  savedGrant = *grant;
  savedKeyValue = *kv;
  appImage = image;
  appAllocation = allocation;
}
static bool owner() { return true; }
static bool health(risc_runtime_health_v1*) { ++healthCalls; return true; }
static bool logLine(const char* text) { test_retained_trace(text); return true; }
static uint64_t now() { return 0; }
static void delay(uint32_t) { ++delays; }
static bool gpioOpen(uint8_t, bool, bool, bool) { return true; }
static bool gpioWrite(uint8_t, bool) { return true; }
static bool gpioRead(uint8_t, bool* value) { *value = true; return true; }
static bool gpioPwm(uint8_t, uint32_t, uint16_t, uint16_t) { return true; }
static bool busClose(uint8_t) { return true; }
static bool i2cOpen(uint8_t, uint8_t, uint8_t, uint32_t) { return true; }
static bool i2cTransfer(uint8_t, uint8_t, const uint8_t*, size_t, uint8_t*, size_t, uint32_t) { return true; }
static bool spiOpen(uint8_t, int16_t, int16_t, int16_t) { return true; }
static bool spiBegin(uint8_t, uint8_t, uint32_t, uint8_t, uint32_t) { return true; }
static bool spiTransfer(uint8_t, const uint8_t*, uint8_t*, size_t, uint32_t) { return true; }
static bool spiEnd(uint8_t, uint8_t, uint32_t) { return true; }
static bool gpioClose(uint8_t pin) {
  assert(pin == 6 || pin == 7);
  test_retained_trace(pin == 6 ? "CPU close-output" : "CPU close-input");
  return true;
}
static bool deepValid(uint8_t pin) { return pin < 22; }
static bool deepReady() { return true; }
static bool deepArm(uint8_t pin, bool high, bool pullup) {
  assert(pin == 7 && !high && pullup);
  const bool ok = mode == "unexpected-return" || mode == "fini-retained" || mode == "init-retained" || mode.find("timed-") == 0;
  test_retained_trace(ok ? "CPU wake-arm" : "CPU wake-arm-failed");
  return ok;
}
static bool deepClear(uint8_t pin, bool pullup) {
  assert(pin == 7 && pullup);
  const bool ok = mode != "wake-clear" && mode.find("crown-clear") == std::string::npos;
  test_retained_trace(ok ? "CPU wake-clear" : "CPU wake-clear-failed");
  return ok;
}
static bool deepHold(uint8_t pin, bool enable) {
  assert(pin == 6);
  const bool ok = mode != "hold-rollback" && (enable || mode != "unhold");
  test_retained_trace(enable ? (ok ? "CPU hold-on" : "CPU hold-on-failed")
                            : (ok ? "CPU hold-off" : "CPU hold-off-failed"));
  return ok;
}
static bool lightArm(uint8_t pin,bool high) { return deepArm(pin,high,true); }
static bool lightClear(uint8_t pin) { return deepClear(pin,true); }
static bool timerArm(uint32_t ms) {
  assert(ms==123);
  const bool ok=mode.find("timed-deep-")!=0 || mode=="timed-deep-return";
  test_retained_trace(ok ? "CPU timer-arm" : "CPU timer-arm-failed");return ok;
}
static bool timerClear() {
  const bool ok=mode.find("timer-clear")==std::string::npos;
  test_retained_trace(ok ? "CPU timer-clear" : "CPU timer-clear-failed");return ok;
}
static bool lightSleep(uint32_t* cause) {
  const bool ok=mode!="timed-light-short";
  test_retained_trace(ok ? "CPU light-return" : "CPU light-refused");
  *cause=RISC_LIGHT_SLEEP_WAKE_TIMER;return ok;
}
static void deepSleep() { test_retained_trace("CPU unexpected-return"); }
static bool bind(RiscBoot::Runtime& runtime) { return cpu->bind(runtime); }
static bool appExitSafe() { return cpu->appExitSafe(); }
static int32_t kvGet(void*, uint32_t, const char*, void*, uint32_t, uint32_t*) {
  ++kvCalls;
  return RISC_KEY_VALUE_NOT_FOUND;
}
static int32_t kvPut(void*, uint32_t ns, const char* key, const void* value, uint32_t size) {
  assert(ns == 1 && !strcmp(key, "probe") && size == 1 && *static_cast<const char*>(value) == 'x');
  ++kvCalls;
  return RISC_KEY_VALUE_OK;
}
static const RiscBoot::KeyValueBackend keyValue{nullptr, kvGet, kvPut};

static void checkRevoked(RiscBoot::Runtime& runtime) {
  assert(!runtime.active() && !risc_runtime_get_api(1));
  risc_runtime_health_v1 snapshot{};
  snapshot.struct_size = sizeof(snapshot);
  risc_runtime_capability_v1 fresh{};
  fresh.struct_size = sizeof(fresh);
  const unsigned previousDelays = delays, previousHealth = healthCalls, previousKv = kvCalls;
  assert(!runtime.health(&snapshot) && !runtime.diagnostic("FORBIDDEN diagnostic"));
  assert(!runtime.launch("queued.elf"));
  assert(!runtime.acquire("test.retained", 1, 7, &fresh));
  assert(!runtime.release(&savedGrant));
  runtime.yield(10);
  if (savedApi.health) {
    assert(!savedApi.health(&snapshot));
    assert(!savedApi.diagnostic("FORBIDDEN saved-diagnostic"));
    assert(!savedApi.request_launch("queued.elf"));
    assert(!savedApi.acquire("test.retained", 1, 7, &fresh));
    assert(!savedApi.release(&savedGrant));
    savedApi.yield_ms(10);
    char value = 'z';
    uint32_t size = 99;
    assert(savedKeyValue.get(savedKeyValue.context, "probe", &value, 1, &size) == RISC_KEY_VALUE_CONTEXT);
    assert(size == 0 && value == 'z');
    assert(savedKeyValue.put(savedKeyValue.context, "probe", &value, 1) == RISC_KEY_VALUE_CONTEXT);
  }
  assert(delays == previousDelays && healthCalls == previousHealth && kvCalls == previousKv);
  test_retained_trace("RUNTIME API-revoked");
}
static void child() {
  RiscCpu::Hardware hardware{};
  hardware.owner = owner; hardware.now = now; hardware.sleep = delay;
  hardware.gpioOpen = gpioOpen; hardware.gpioWrite = gpioWrite;
  hardware.gpioRead = gpioRead; hardware.gpioPwm = gpioPwm; hardware.gpioClose = gpioClose;
  hardware.i2cOpen = i2cOpen; hardware.i2cTransfer = i2cTransfer; hardware.i2cClose = busClose;
  hardware.spiOpen = spiOpen; hardware.spiBegin = spiBegin; hardware.spiTransfer = spiTransfer;
  hardware.spiEnd = spiEnd; hardware.spiClose = busClose;
  hardware.deepWakeValid = deepValid; hardware.deepReady = deepReady;
  hardware.deepWakeArm = deepArm; hardware.deepWakeClear = deepClear;
  hardware.deepSleep = deepSleep; hardware.deepHold = deepHold;
  hardware.wakeValid=deepValid;hardware.wakeArm=lightArm;hardware.wakeClear=lightClear;hardware.lightSleep=lightSleep;
  hardware.timerArm=timerArm;hardware.timerClear=timerClear;
  cpu = new RiscCpu::Port(hardware);
  auto* runtime = new RiscBoot::Runtime({owner, health, delay, logLine, bind, &keyValue, appExitSafe});
  assert(runtime->prepare(root.c_str()));
  const bool ordinary = mode == "ordinary-refusal" || mode == "timed-light-normal" || mode == "timed-light-short" || mode == "timed-deep-refusal";
  assert(runtime->run() == ordinary);
  assert(cpu->appExitSafe() == ordinary);
  assert(cpu->quiescent() == ordinary);
  checkRevoked(*runtime);
  if (ordinary) {
    assert(defaultRuns == 2 && kvCalls == 1);
    delete runtime;
    delete cpu;
    test_retained_trace("RUNTIME ordinary-complete");
  } else {
    assert(strstr(runtime->error(), "native retention barrier"));
    assert(defaultRuns == (mode == "initial-held" ? 0u : 1u));
    if (mode != "initial-held") {
      // Pinned image and heap witness remain readable; no callback or allocator
      // teardown has happened. Native allocator ordering is checked separately.
      Dl_info image{}, provider{};
      assert(dladdr(appImage, &image) && dladdr(savedGrant.api, &provider));
      assert(image.dli_fbase != provider.dli_fbase);
      assert(!strcmp(appAllocation, "still retained"));
      assert(kvCalls == 1);
    }
    assert(!runtime->run());
    test_retained_trace("RUNTIME rejected-restart");
  }
  // Retained images/graph intentionally stay alive. Normal C++/loader process
  // teardown would incorrectly release them and destroy the failure evidence.
  _exit(0);
}
static void file(const char* name, const std::string& content) {
  std::ofstream(root + "/" + name) << content;
}
static std::string readTrace() {
  std::ifstream input(tracePath);
  return {std::istreambuf_iterator<char>(input), {}};
}
static void runChild(const char* executable, const char* scenario) {
  file("retained-trace.txt", "");
  const pid_t pid = fork();
  assert(pid >= 0);
  if (!pid) {
    execl(executable, executable, root.c_str(), scenario, static_cast<char*>(nullptr));
    _exit(99);
  }
  int status = 0;
  assert(waitpid(pid, &status, 0) == pid);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "%s failed (status=%d):\n%s", scenario, status, readTrace().c_str());
    assert(false);
  }
}
static std::string expected(const std::string& scenario) {
  const std::string provider = "PROVIDER loaded\nPROVIDER started\n";
  if (scenario == "initial-held")
    return provider + "CPU hold-on\nRUNTIME API-revoked\nRUNTIME rejected-restart\n";
  const std::string launchPrefix = provider +
    "DEFAULT loaded\nDEFAULT init\nDEFAULT main\nDEFAULT queued-clock\nDEFAULT fini\nDEFAULT unloaded\n"
    "CLOCK loaded\nCLOCK init\n";
  if (scenario == "init-retained")
    return launchPrefix + "CLOCK queued-child\nCPU hold-on\nCPU wake-arm\nCPU unexpected-return\n"
      "CLOCK init-retained\nRUNTIME API-revoked\nRUNTIME rejected-restart\n";
  const std::string prefix = launchPrefix + "CLOCK main\nCLOCK queued-child\n";
  std::string attempt;
  if (scenario.find("timed-")==0) {
    attempt="CPU hold-on\nCPU wake-arm\n";
    const bool light=scenario.find("timed-light-")==0;
    if(light) attempt+="CPU timer-arm\n" + std::string(scenario=="timed-light-short" ? "CPU light-refused\n" : "CPU light-return\n");
    else if(scenario=="timed-deep-return") attempt+="CPU timer-arm\nCPU unexpected-return\n";
    else attempt+="CPU timer-arm-failed\n";
    attempt+=scenario.find("timer-clear")!=std::string::npos ? "CPU timer-clear-failed\n" : "CPU timer-clear\n";
    attempt+=scenario.find("crown-clear")!=std::string::npos ? "CPU wake-clear-failed\n" : "CPU wake-clear\n";
    if(scenario=="timed-light-normal" || scenario=="timed-light-short" || scenario=="timed-deep-refusal") attempt+="CPU hold-off\n";
  }
  else if (scenario == "unexpected-return" || scenario == "fini-retained")
    attempt = "CPU hold-on\nCPU wake-arm\nCPU unexpected-return\n";
  else if (scenario == "hold-rollback")
    attempt = "CPU hold-on-failed\nCPU hold-off-failed\n";
  else if (scenario == "wake-clear")
    attempt = "CPU hold-on\nCPU wake-arm-failed\nCPU wake-clear-failed\n";
  else {
    attempt = "CPU hold-on\nCPU wake-arm-failed\nCPU wake-clear\n";
    if (scenario == "unhold") attempt += "CPU hold-off-failed\n";
    if (scenario == "ordinary-refusal") attempt += "CPU hold-off\n";
  }
  if (scenario == "ordinary-refusal" || scenario == "timed-light-normal" || scenario == "timed-light-short" || scenario == "timed-deep-refusal")
    return prefix + attempt + (scenario=="timed-light-normal" ? "CLOCK sleep-returned\n" : "CLOCK sleep-refused\n") +
      "CLOCK fini\nCLOCK unloaded\n"
      "QUEUED loaded\nQUEUED init\nQUEUED main\nQUEUED fini\nQUEUED unloaded\n"
      "DEFAULT loaded\nDEFAULT init\nDEFAULT main\nDEFAULT fini\nDEFAULT unloaded\n"
      "PROVIDER quiesce-attempt\nCPU close-output\nCPU close-input\nPROVIDER quiesced\n"
      "PROVIDER stopped\nPROVIDER unloaded\nRUNTIME API-revoked\nRUNTIME ordinary-complete\n";
  if (scenario == "fini-retained")
    return prefix + "CLOCK defer-sleep-to-fini\nCLOCK fini\n" + attempt +
      "CLOCK fini-retained\nRUNTIME API-revoked\nRUNTIME rejected-restart\n";
  return prefix + attempt + (scenario == "held-output" ? "CLOCK sleep-refused\n" : "CLOCK sleep-retained\n") +
    "RUNTIME API-revoked\nRUNTIME rejected-restart\n";
}
int main(int argc, char** argv) {
  assert(argc == 2 || argc == 3);
  root = argv[1]; tracePath = root + "/retained-trace.txt";
  if (argc == 3) { mode = argv[2]; child(); }
  file("board.json", R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[{"instance_id":7,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[7,6],"active_high":true,"pull_up":true,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})");
  file("retained.json", R"({"type":"driver","id":"retained-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"retained.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.gpio","api":1}],"provides":[{"capability":"test.retained","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})");
  file("clock.json", R"({"type":"application","id":"retained-clock","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"clock.elf","entry":"app_main","requires":[{"capability":"test.retained","api":1},{"capability":"storage.key-value","api":1}]})");
  file("boot.json", R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"retained.json","instance_id":7}],"app_capabilities":[{"manifest":"clock.json","grants":[{"capability":"test.retained","api":1,"instance_id":7},{"capability":"storage.key-value","api":1,"instance_id":1}]}]})");
  for (const char* scenario : {"unexpected-return", "hold-rollback", "unhold", "wake-clear",
                               "held-output", "fini-retained", "init-retained", "initial-held", "ordinary-refusal",
                               "timed-light-timer-clear", "timed-light-crown-clear", "timed-deep-timer-clear",
                               "timed-deep-crown-clear", "timed-deep-return", "timed-light-normal", "timed-light-short", "timed-deep-refusal"}) {
    runChild(argv[0], scenario);
    const std::string actual = readTrace(), wanted = expected(scenario);
    if (actual != wanted)
      fprintf(stderr, "%s trace mismatch\nExpected:\n%sActual:\n%s", scenario, wanted.c_str(), actual.c_str());
    assert(actual == wanted);
    printf("Actual runtime + CPU + dynamic clock/provider: %s PASS\n", scenario);
  }
  puts("Retained invocations keep images/memory/boot grants, revoke app APIs and block all handoffs; ordinary refusal unwinds and reloads PASS");
}
