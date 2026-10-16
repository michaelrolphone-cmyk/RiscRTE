// Complete Runtime prepare + real GraphV2 host ELF mapping + real CpuPort bind.
// No private-state construction, borrowed graph grants or fake bound Ports.
#include "experimental/native_replica/NativeRuntimeStoragePin.h"
#include <algorithm>
#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "RuntimeFileFixture.h"

using Pin = RiscExperimental::NativeRuntimeStoragePin;
static std::string mode;
static bool owned = true;
static unsigned hardwareCalls = 0, reentries = 0, metadataAllocations = 0;
#if RISC_NATIVE_SETTINGS_LIFECYCLE
static unsigned ownerCheckpoints=0;
#endif
namespace RiscBoot {
void *metadataTestAllocate(size_t bytes) {
    ++metadataAllocations;
    // This fixture has no app policies: allocation 1 is installation state;
    // allocation 2 is the final installed-app registry reservation.
    if (mode == "allocation-failed" && metadataAllocations == 2) return nullptr;
    return std::malloc(bytes);
}
}
static RiscCpu::Port *cpu;
static RiscBoot::Runtime *runtime;
static Pin *activePin;
static const void *selectedImage, *dependencyImage;
struct Event { std::string provider, phase; };
static std::vector<Event> events;
template <class T> static void expectAbort(T *);
extern "C" const char *native_pin_mode() { return mode.c_str(); }
extern "C" void native_pin_event(const char *provider, const char *phase, const void *image) {
    events.push_back({provider, phase});
    if (!std::strcmp(phase, "start")) {
        if (!std::strcmp(provider, "selected-storage")) selectedImage = image;
        if (!std::strcmp(provider, "pin-dependency")) dependencyImage = image;
    }
}
static unsigned count(const char *provider, const char *phase) {
    return std::count_if(events.begin(), events.end(), [&](const Event &event) {
        return event.provider == provider && event.phase == phase;
    });
}
static size_t first(const char *provider, const char *phase) {
    for (size_t i = 0; i < events.size(); ++i)
        if (events[i].provider == provider && events[i].phase == phase) return i;
    assert(false); return 0;
}
static bool mapped(const void *address) { Dl_info info{}; return address && dladdr(address, &info); }
extern "C" void native_pin_reenter(const char *phase) {
    if (mode == "destructor-acquiring" && !std::strcmp(phase, "start")) {
        assert(activePin->state() == Pin::State::Acquiring && !activePin->exitSafe());
        expectAbort(activePin);
    }
    if (mode == "destructor-releasing" && !std::strcmp(phase, "quiesce")) {
        assert(activePin->state() == Pin::State::Releasing && !activePin->exitSafe());
        expectAbort(activePin);
    }
    if (mode != "reentry") return;
    assert(activePin && runtime && cpu);
    const auto state = activePin->state();
    const auto live = runtime->grantUsage().graphLive;
    assert(activePin->acquire() != RISC_STORAGE_ASYNC_OK);
    assert(activePin->release() != RISC_STORAGE_ASYNC_OK);
    assert(activePin->state() == state && runtime->grantUsage().graphLive == live);
    Pin other(*runtime, *cpu, 55);
    assert(other.acquire() != RISC_STORAGE_ASYNC_OK && other.exitSafe());
    assert(runtime->grantUsage().graphLive == live);
    ++reentries;
}
static bool owner() { return owned; }
static RiscCpu::Hardware hardware() {
    RiscCpu::Hardware h{};
    h.owner = owner;
    h.now = []() -> uint64_t { return 0; };
    h.sleep = [](uint32_t) { ++hardwareCalls; };
    h.gpioOpen = [](uint8_t, bool, bool, bool) { ++hardwareCalls; return true; };
    h.gpioWrite = [](uint8_t, bool) { ++hardwareCalls; return true; };
    h.gpioRead = [](uint8_t, bool *) { ++hardwareCalls; return true; };
    h.gpioPwm = [](uint8_t, uint32_t, uint16_t, uint16_t) { ++hardwareCalls; return true; };
    h.gpioClose = [](uint8_t) { ++hardwareCalls; return true; };
    h.i2cOpen = [](uint8_t, uint8_t, uint8_t, uint32_t) { ++hardwareCalls; return true; };
    h.i2cTransfer = [](uint8_t, uint8_t, const uint8_t *, size_t, uint8_t *, size_t, uint32_t) { ++hardwareCalls; return true; };
    h.i2cClose = [](uint8_t) { ++hardwareCalls; return true; };
    h.spiOpen = [](uint8_t, int16_t, int16_t, int16_t) { ++hardwareCalls; return true; };
    h.spiBegin = [](uint8_t, uint8_t, uint32_t, uint8_t, uint32_t) { ++hardwareCalls; return true; };
    h.spiTransfer = [](uint8_t, const uint8_t *, uint8_t *, size_t, uint32_t) { ++hardwareCalls; return true; };
    h.spiEnd = [](uint8_t, uint8_t, uint32_t) { ++hardwareCalls; return true; };
    h.spiClose = [](uint8_t) { ++hardwareCalls; return true; };
    return h;
}
static RiscBoot::Port bootPort(bool bind = true) {
    RiscBoot::Port port{owner, [](risc_runtime_health_v1 *) { return true; },
        [](uint32_t) {}, [](const char *) { return true; }};
    if (bind) port.bindPlatforms = [](RiscBoot::Runtime &r) { return cpu->bind(r); };
#if RISC_NATIVE_SETTINGS_LIFECYCLE
    if(mode=="owner-checkpoint")port.nativeOwnerCheckpoint=[](bool admitNew){
      assert(admitNew);
        assert(runtime&&!runtime->active()&&!runtime->residentResetSafe());
        Pin pin(*runtime,*cpu,55);assert(pin.acquire()==RISC_STORAGE_ASYNC_OK&&pin.held());
        assert(pin.volume()&&pin.device()==55&&runtime->grantUsage().graphLive==1);
        assert(pin.release()==RISC_STORAGE_ASYNC_OK&&pin.exitSafe());
        assert(!runtime->grantUsage().graphLive&&!runtime->retained());++ownerCheckpoints;return true;
    };
#endif
    return port;
}
static void save(const std::string &root, const char *path, const std::string &value) {
    std::ofstream file(root + "/" + path);
    file << value;
    assert(file.good());
}
static void metadata(const std::string &root) {
    save(root, "board.json", R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"pin-test","revision":"unspecified","buses":[],"devices":[{"instance_id":55,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[5],"active_high":true,"pull_up":false,"debounce_us":0,"long_press_us":0,"click_min_us":0},"bindings":{"test.pin-dependency":7}},{"instance_id":99,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[9],"active_high":true,"pull_up":false,"debounce_us":0,"long_press_us":0,"click_min_us":0},"bindings":{"test.pin-dependency":7}},{"instance_id":7,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[8],"active_high":true,"pull_up":false,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})");
    const auto manifest = [](const char *id, const char *image) {
        return std::string(R"({"type":"driver","id":")") + id +
            R"(","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":")" + image +
            R"(","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.sync","api":1},{"capability":"test.pin-dependency","api":1}],"provides":[{"capability":"storage.volume","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})";
    };
    save(root, "selected.json", manifest("selected-storage", mode == "missing-image" ? "missing.elf" : mode == "wrong-image" ? "foreign.elf" : "selected.elf"));
    save(root, "foreign.json", manifest("foreign-storage", "foreign.elf"));
    std::string dependency = R"({"type":"driver","id":"pin-dependency","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"dependency.elf","requires":[{"capability":"hardware.device","api":1}],"provides":[{"capability":"test.pin-dependency","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})";
    if (mode == "invalid-graph") dependency.replace(dependency.find("\"requires\":["), 12, R"("requires":[{"capability":"test.missing","api":1},)");
    save(root, "dependency.json", dependency);
    std::string boot = R"({"board":"board.json","default_app":"default.elf","provider_activation":"demand","drivers":[{"manifest":"foreign.json","instance_id":99},{"manifest":"selected.json","instance_id":55},{"manifest":"dependency.json","instance_id":7}])";
    if (mode != "no-installation") boot += std::string(R"(,"installation":{"volume_instance":)") + (mode == "wrong-installation" ? "99" : "55") + R"(,"grants":[]})";
    boot += "}";
    if (mode == "invalid-board") save(root, "board.json", "{}");
    save(root, "boot.json", boot);
}
static void released(const Pin &pin) {
    assert(pin.state() == Pin::State::Released && pin.exitSafe());
    assert(!pin.held() && !pin.volume() && !pin.token());
}
static void retained(Pin &pin, bool grant) {
    assert(pin.state() == Pin::State::Retained && !pin.exitSafe() && runtime->retained());
    assert(!pin.held() && !pin.volume() && !pin.token());
    assert(runtime->grantUsage().graphLive == (grant ? 1u : 0u));
    const auto before = events.size();
    assert(pin.release() == RISC_STORAGE_ASYNC_RETAINED);
    assert(pin.acquire() != RISC_STORAGE_ASYNC_OK && events.size() == before);
    Pin other(*runtime, *cpu, 55);
    assert(other.acquire() != RISC_STORAGE_ASYNC_OK && other.exitSafe());
    assert(!hardwareCalls);
}
template <class T> static void expectAbort(T *pin) {
    const pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        const rlimit limit{0, 0};
        assert(setrlimit(RLIMIT_CORE, &limit) == 0);
        delete pin;
        std::_Exit(91);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}
int main(int argc, char **argv) {
    assert(argc == 3);
    const std::string root = argv[1]; mode = argv[2]; metadata(root);
    cpu = new RiscCpu::Port(hardware());
    static RiscTest::FileSources runtimeFiles0({root.c_str()});runtime = new RiscBoot::Runtime(bootPort());assert(runtimeFiles0.bind(*runtime));
    if (mode == "unprepared") {
        Pin pin(*runtime, *cpu, 55);
        assert(pin.acquire() != RISC_STORAGE_ASYNC_OK && pin.exitSafe() && events.empty());
    } else {
        const bool valid = mode != "invalid-board" && mode != "invalid-graph" && mode != "allocation-failed";
        const bool prepared = runtime->prepare(root.c_str());
        if (prepared != valid) std::fprintf(stderr, "prepare: %s\n", runtime->error());
        assert(prepared == valid && events.empty() && !hardwareCalls);
        if (mode == "allocation-failed") {
            assert(metadataAllocations == 4);
            assert(!std::strcmp(runtime->error(), "installed registry reservation failed"));
        }
#if RISC_NATIVE_SETTINGS_LIFECYCLE
        if(mode=="owner-checkpoint"){
            assert(prepared&&runtime->run()&&!runtime->retained()&&ownerCheckpoints);
            assert(!runtime->grantUsage().graphLive&&!hardwareCalls);
            delete runtime;delete cpu;
            puts("Runtime graph pin: actual owner checkpoint admission with app authority hidden PASS");return 0;
        }
#endif
        auto *pin = new Pin(*runtime, *cpu, mode == "wrong-device" ? 99 : mode == "zero-device" ? 0 : 55);
        activePin = pin;
        assert(pin->exitSafe() && !pin->held() && !pin->volume() && !pin->token());
        if (!valid || mode == "no-installation" || mode == "wrong-installation" ||
            mode == "wrong-device" || mode == "zero-device") {
            assert(pin->acquire() != RISC_STORAGE_ASYNC_OK && pin->exitSafe());
            assert(runtime->grantUsage().graphLive == 0 && events.empty());
        } else if (mode == "wrong-port" || mode == "wrong-runtime") {
            RiscCpu::Port wrong(hardware());
            RiscBoot::Runtime wrongRuntime(bootPort(false));
            Pin mismatch(mode == "wrong-runtime" ? wrongRuntime : *runtime,
                         mode == "wrong-port" ? wrong : *cpu, 55);
            assert(!mismatch.owner() && mismatch.acquire() != RISC_STORAGE_ASYNC_OK && mismatch.exitSafe());
            assert(events.empty());
        } else if (mode == "wrong-owner") {
            owned = false;
            assert(!pin->owner() && pin->acquire() != RISC_STORAGE_ASYNC_OK && pin->exitSafe());
            owned = true; assert(events.empty());
        } else {
            const int result = pin->acquire();
            if (mode == "missing-image" || mode == "wrong-image" || mode == "start-failed") {
                assert(result != RISC_STORAGE_ASYNC_OK && result != RISC_STORAGE_ASYNC_RETAINED);
                assert(pin->exitSafe() && !runtime->retained() && !runtime->grantUsage().graphLive);
                assert(!mapped(selectedImage) && !mapped(dependencyImage));
                if (mode == "start-failed") {
                    assert(pin->state() == Pin::State::Idle);
                    mode = "happy";
                    assert(pin->acquire() == 0 && pin->held());
                    assert(pin->release() == 0); released(*pin);
                    mode = "start-failed";
                }
            } else if (mode == "start-retained" || mode == "invalid-suffix-retained") {
                assert(result == RISC_STORAGE_ASYNC_RETAINED);
                retained(*pin, mode == "invalid-suffix-retained");
                assert(mapped(selectedImage) && mapped(dependencyImage));
                assert(!count("selected-storage", "stop") && !count("selected-storage", "unload"));
                assert(!count("pin-dependency", "quiesce"));
                expectAbort(pin);
                std::printf("Runtime graph pin %s: retained mappings and destructor contract PASS\n", mode.c_str());
                std::fflush(stdout); std::_Exit(0);
            } else if (mode == "invalid-suffix") {
                assert(result == RISC_STORAGE_ASYNC_INVALID);
                released(*pin);
                assert(!runtime->retained() && !runtime->grantUsage().graphLive);
                assert(count("selected-storage", "stop") == 1 && !mapped(selectedImage));
            } else {
                assert(result == RISC_STORAGE_ASYNC_OK && pin->held() && !pin->exitSafe());
                assert(pin->device() == 55 && pin->volume() == selectedImage && pin->token());
                const auto *device = static_cast<const risc_hardware_device_v1 *>(pin->volume()->context);
                assert(device == &runtime->board().device(55)->hardware && device->instance_id == 55);
                assert(runtime->grantUsage().graphLive == 1 && mapped(selectedImage) && mapped(dependencyImage));
                assert(count("selected-storage", "start") == 1 && count("foreign-storage", "start") == 0);
                assert(first("pin-dependency", "start") < first("selected-storage", "start"));
                const auto token = pin->token(), before = events.size();
                assert(pin->acquire() != RISC_STORAGE_ASYNC_OK && pin->token() == token && events.size() == before);
                owned = false;
                assert(!pin->held() && !pin->volume() && !pin->token());
                assert(pin->release() != RISC_STORAGE_ASYNC_OK && !pin->exitSafe());
                owned = true; assert(pin->held() && pin->token() == token);
                if (mode == "destructor-held") expectAbort(pin);
                if (mode == "destructor-runtime") expectAbort(runtime);
                if (mode == "grant-capacity") {
                    std::vector<std::unique_ptr<Pin>> pins;
                    for (size_t i = 1; i < RuntimeProviders::GraphV2::kMaxGrants; ++i) {
                        pins.emplace_back(new Pin(*runtime, *cpu, 55));
                        assert(pins.back()->acquire() == 0 && pins.back()->held());
                        assert(pins.back()->token() != token);
                    }
                    assert(runtime->grantUsage().graphLive == RuntimeProviders::GraphV2::kMaxGrants);
                    Pin overflow(*runtime, *cpu, 55);
                    assert(overflow.acquire() == RISC_STORAGE_ASYNC_BUSY && overflow.exitSafe());
                    assert(!runtime->retained() && pin->held());
                    for (auto &other : pins) { assert(other->release() == 0); released(*other); }
                    assert(!count("selected-storage", "quiesce") && pin->held());
                    assert(pin->release() == 0); released(*pin);
                } else if (mode == "multiple-grants") {
                    Pin other(*runtime, *cpu, 55);
                    assert(other.acquire() == 0 && other.held() && other.token() != token);
                    assert(other.volume() == pin->volume() && runtime->grantUsage().graphLive == 2);
                    assert(pin->release() == 0); released(*pin);
                    assert(other.held() && runtime->grantUsage().graphLive == 1);
                    assert(count("selected-storage", "quiesce") == 0 && mapped(selectedImage));
                    assert(other.release() == 0); released(other);
                } else if (mode == "release-retained" || mode == "dependency-retained" || mode == "destructor-retained") {
                    assert(pin->release() == RISC_STORAGE_ASYNC_RETAINED);
                    retained(*pin, true);
                    assert(mapped(dependencyImage) && !count("pin-dependency", "stop"));
                    if (mode == "dependency-retained") {
                        assert(!mapped(selectedImage) && count("selected-storage", "stop") == 1);
                    } else {
                        assert(mapped(selectedImage) && !count("selected-storage", "stop"));
                        assert(!count("pin-dependency", "quiesce"));
                    }
                    expectAbort(pin);
                    std::printf("Runtime graph pin %s: retained mappings and destructor contract PASS\n", mode.c_str());
                    std::fflush(stdout); std::_Exit(0);
                } else {
                    assert(pin->release() == 0); released(*pin);
                }
                assert(!runtime->grantUsage().graphLive && !runtime->retained());
                assert(!mapped(selectedImage) && !mapped(dependencyImage));
                assert(count("selected-storage", "stop") == 1 && count("selected-storage", "unload") == 1);
                assert(first("selected-storage", "unload") < first("pin-dependency", "quiesce"));
                assert(count("pin-dependency", "stop") == 1 && count("pin-dependency", "unload") == 1);
                if (mode == "reentry") assert(reentries == 3);
                assert(pin->release() != RISC_STORAGE_ASYNC_OK && pin->acquire() != RISC_STORAGE_ASYNC_OK);
                if (mode == "fresh-generation") {
                    Pin next(*runtime, *cpu, 55); activePin = &next;
                    assert(next.acquire() == 0 && next.token() != token && next.held());
                    assert(next.release() == 0); released(next);
                }
            }
        }
        delete pin; activePin = nullptr;
    }
    assert(!hardwareCalls);
    delete runtime; delete cpu;
    std::printf("Runtime graph pin %s PASS (complete prepare, exact module/device, zero media I/O)\n", mode.c_str());
}
