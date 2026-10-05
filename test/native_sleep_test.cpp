#include "ports/esp32s3/NativeSleep.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

uint64_t native_sleep_test_output_mask = native_sleep_test_gpio_mask;

namespace {
using namespace RiscCpu::NativeSleep;
const char* scenario = "initialization";
#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, scenario, #condition); \
  std::abort(); } } while (false)

struct Event {
  std::string name;
  int pin;
  uint64_t arg;
  gpio_config_t config{};
};
struct Pad {
  bool held = false;
  bool configured = false;
  bool level = false;
  bool rtc = false;
  bool rtcPullup = false;
  bool rtcPulldown = false;
  gpio_pull_mode_t pull = GPIO_FLOATING;
  gpio_config_t config{};
};
std::vector<Event> calls;
std::map<std::string, esp_err_t> errors;
std::array<Pad, GPIO_NUM_MAX> pads;
bool globalHold = false;
bool internalStack = true;
uint64_t wakeMask = 0, timerUs = 0;
bool gpioSource = false;
int ext0Pin=-1,ext0Level=-1,failCall=-1;
esp_sleep_wakeup_cause_t lightCause = ESP_SLEEP_WAKEUP_TIMER;
esp_sleep_ext1_wakeup_mode_t wakeMode = ESP_EXT1_WAKEUP_ANY_LOW;
esp_sleep_pd_option_t power = ESP_PD_OPTION_AUTO;
struct DeepSleepEntered {};

void reset(const char* name) {
  scenario = name;
  calls.clear(); errors.clear(); pads = {}; ext0Pin=ext0Level=failCall=-1;
  globalHold = false; internalStack = true; wakeMask = timerUs = 0; gpioSource = false; power = ESP_PD_OPTION_AUTO;
  native_sleep_test_output_mask = native_sleep_test_gpio_mask;
}
esp_err_t record(const char* name, int pin = -1, uint64_t arg = 0) {
  calls.push_back({name, pin, arg, {}});
  if(int(calls.size())==failCall)return ESP_FAIL;
  const auto found = errors.find(name);
  return found == errors.end() ? ESP_OK : found->second;
}
bool called(const char* name) {
  return std::any_of(calls.begin(), calls.end(), [&](const Event& e) { return e.name == name; });
}
void names(std::initializer_list<const char*> expected) {
  CHECK(calls.size() == expected.size());
  size_t i = 0;
  for (const auto* name : expected) CHECK(calls[i++].name == name);
}
void noGlobalHold() {
  CHECK(!globalHold);
  CHECK(!called("deep_hold_en") && !called("deep_hold_dis") && !called("deep_sleep_start"));
}
void checkConfig(uint8_t pin, bool output, bool pullup) {
  const auto& p = pads[pin];
  CHECK(p.configured);
  CHECK(p.config.pin_bit_mask == (uint64_t(1) << pin));
  CHECK(p.config.mode == (output ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT));
  CHECK(p.config.pull_up_en == (pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE));
  CHECK(p.config.pull_down_en == GPIO_PULLDOWN_DISABLE);
  CHECK(p.config.intr_type == GPIO_INTR_DISABLE);
}
void checkClearCalls(bool pullup) {
  names({"disable_ext1", "rtc_pullup_dis", "rtc_pulldown_dis", "rtc_deinit", "digital_pull", "power"});
  CHECK(calls[0].arg == ESP_SLEEP_WAKEUP_EXT1);
  for (size_t i = 1; i != 5; ++i) CHECK(calls[i].pin == 21);
  CHECK(calls[4].arg == uint64_t(pullup ? GPIO_PULLUP_ONLY : GPIO_FLOATING));
  CHECK(calls[5].pin == ESP_PD_DOMAIN_RTC_PERIPH && calls[5].arg == ESP_PD_OPTION_AUTO);
  noGlobalHold();
}

void testValidity() {
  reset("S3 RTC wake eligibility");
  for (unsigned pin = 0; pin != 256; ++pin) CHECK(valid(uint8_t(pin)) == (pin <= 21));
  CHECK(calls.empty());
  // Bad pins cannot reach IDF's unguarded bit shifts or perform hardware writes.
  for (uint8_t pin : {22, 23, 24, 25, 49, 63, 64, 127, 255}) {
    CHECK(!openPin(pin, false, false, false));
    CHECK(!openPin(pin, true, true, false));
  }
  CHECK(calls.empty());
  noGlobalHold();
}

void testOpen() {
  for (const uint8_t pin : {uint8_t(21), uint8_t(45), uint8_t(46), uint8_t(48)}) {
    for (bool output : {false, true}) for (bool initial : {false, true}) for (bool pullup : {false, true}) {
      reset("open configures held GPIO before unhold");
      pads[pin].held = true; pads[pin].rtc = pin <= 21;
      CHECK(canClose(pin));
      CHECK(openPin(pin, output, initial, pullup));
      CHECK(canClose(pin));
      if (pin <= 21 && output) names({"rtc_deinit", "set_level", "config", "hold_dis"});
      else if (pin <= 21) names({"rtc_deinit", "config", "hold_dis"});
      else if (output) names({"set_level", "config", "hold_dis"});
      else names({"config", "hold_dis"});
      for (const auto& event : calls) CHECK(event.pin == pin);
      checkConfig(pin, output, pullup);
      if (output) CHECK(pads[pin].level == initial);
      CHECK(!pads[pin].held && !pads[pin].rtc);
      noGlobalHold();

      // Every SDK failure must be observable, with an existing hold retained.
      const auto successful = calls;
      for (size_t i = 0; i != successful.size(); ++i) {
        reset("open stage failure retains hold");
        pads[pin].held = true;
        errors[successful[i].name] = ESP_FAIL;
        CHECK(!openPin(pin, output, initial, pullup));
        CHECK(!canClose(pin));
        CHECK(calls.size() == i + 1);
        for (size_t j = 0; j != calls.size(); ++j) CHECK(calls[j].name == successful[j].name);
        CHECK(pads[pin].held);
        if (successful[i].name != "hold_dis") CHECK(!called("hold_dis"));
        noGlobalHold();
        // Checking close eligibility must not release a held/unknown pad.
        CHECK(!canClose(pin) && pads[pin].held);
        CHECK(calls.size() == i + 1);
        errors.clear(); calls.clear();
        CHECK(openPin(pin, output, initial, pullup));
        CHECK(canClose(pin) && !pads[pin].held);
        CHECK(calls.size() == successful.size());
        for (size_t j = 0; j != calls.size(); ++j) CHECK(calls[j].name == successful[j].name);
        checkConfig(pin, output, pullup);
        noGlobalHold();
      }
    }
  }

  reset("synthetic input-only capability rejects output and avoids unsupported unhold");
  native_sleep_test_output_mask &= ~(uint64_t(1) << 46);
  CHECK(!openPin(46, true, true, false));
  CHECK(canClose(46)); // Rejected before touching hardware: no partial open.
  CHECK(calls.empty());
  CHECK(openPin(46, false, false, true));
  CHECK(canClose(46));
  names({"config"});
  checkConfig(46, false, true);
  noGlobalHold();

  reset("failed-open latch belongs to one pad and survives rejected retry");
  pads[45].held = true;
  errors["config"] = ESP_FAIL;
  CHECK(!openPin(45, true, true, false));
  CHECK(!canClose(45));
  errors.clear(); calls.clear();
  CHECK(openPin(21, false, false, true));
  CHECK(canClose(21) && !canClose(45) && pads[45].held);
  const auto beforeInvalidRetry = calls.size();
  native_sleep_test_output_mask &= ~(uint64_t(1) << 45);
  CHECK(!openPin(45, true, true, false));
  CHECK(!canClose(45) && pads[45].held && calls.size() == beforeInvalidRetry);
  native_sleep_test_output_mask = native_sleep_test_gpio_mask;
  CHECK(openPin(45, true, true, false));
  CHECK(canClose(45) && !pads[45].held);
  noGlobalHold();
}

void testArm() {
  for (const uint8_t pin : {uint8_t(0), uint8_t(21)}) {
    for (bool active : {false, true}) for (bool pullup : {false, true}) {
      reset("EXT1 single RTC pin, polarity and claimed pull policy");
      pads[pin].rtcPullup = !pullup; pads[pin].rtcPulldown = true;
      CHECK(arm(pin, active, pullup));
      if (pullup) names({"power", "rtc_pulldown_dis", "rtc_pullup_en", "enable_ext1"});
      else names({"power", "rtc_pulldown_dis", "rtc_pullup_dis", "enable_ext1"});
      CHECK(calls[0].pin == ESP_PD_DOMAIN_RTC_PERIPH);
      CHECK(power == (pullup ? ESP_PD_OPTION_ON : ESP_PD_OPTION_AUTO));
      CHECK(calls[1].pin == pin && calls[2].pin == pin);
      CHECK(wakeMask == (uint64_t(1) << pin));
      CHECK(wakeMode == (active ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW));
      CHECK(pads[pin].rtcPullup == pullup && !pads[pin].rtcPulldown);
      noGlobalHold();
      const auto successful = calls;
      for (size_t i = 0; i != successful.size(); ++i) {
        reset("arm stage failure stops before unsafe entry");
        errors[successful[i].name] = ESP_FAIL;
        CHECK(!arm(pin, active, pullup));
        CHECK(calls.size() == i + 1);
        for (size_t j = 0; j != calls.size(); ++j) CHECK(calls[j].name == successful[j].name);
        CHECK(wakeMask == 0);
        noGlobalHold();
        // The caller cancels a partially configured attempt through clear().
        errors.clear(); calls.clear();
        CHECK(clear(pin, pullup));
        CHECK(power == ESP_PD_OPTION_AUTO && wakeMask == 0);
        CHECK(!pads[pin].rtcPullup && !pads[pin].rtcPulldown);
        CHECK(pads[pin].pull == (pullup ? GPIO_PULLUP_ONLY : GPIO_FLOATING));
        noGlobalHold();
      }
    }
  }
}

void testClear() {
  for (bool pullup : {false, true}) {
    reset("cleanup restores RTC/digital pulls and AUTO policy");
    pads[21].rtc = true; pads[21].rtcPullup = true; pads[21].rtcPulldown = true;
    power = ESP_PD_OPTION_ON; wakeMask = uint64_t(1) << 21;
    CHECK(clear(21, pullup));
    checkClearCalls(pullup);
    CHECK(wakeMask == 0 && power == ESP_PD_OPTION_AUTO);
    CHECK(!pads[21].rtc && !pads[21].rtcPullup && !pads[21].rtcPulldown);
    CHECK(pads[21].pull == (pullup ? GPIO_PULLUP_ONLY : GPIO_FLOATING));
    const auto successful = calls;
    for (const auto& failed : successful) {
      for (esp_err_t error : {ESP_FAIL, ESP_ERR_INVALID_STATE}) {
        reset("cleanup attempts all stages after any failure");
        errors[failed.name] = error;
        const bool alreadyDisabled = failed.name == "disable_ext1" && error == ESP_ERR_INVALID_STATE;
        CHECK(clear(21, pullup) == alreadyDisabled);
        checkClearCalls(pullup);
        errors.clear(); calls.clear();
        CHECK(clear(21, pullup));
        checkClearCalls(pullup);
      }
    }
    reset("cleanup attempts all stages even when every stage fails");
    for (const auto& event : successful) errors[event.name] = ESP_FAIL;
    CHECK(!clear(21, pullup));
    checkClearCalls(pullup);
  }

  reset("repeated arm and clear do not leak forced RTC power or pulls");
  for (bool pullup : {true, false, true, false}) {
    CHECK(arm(21, false, pullup));
    CHECK(power == (pullup ? ESP_PD_OPTION_ON : ESP_PD_OPTION_AUTO));
    CHECK(clear(21, pullup));
    CHECK(power == ESP_PD_OPTION_AUTO && wakeMask == 0);
    CHECK(!pads[21].rtcPullup && !pads[21].rtcPulldown);
    CHECK(pads[21].pull == (pullup ? GPIO_PULLUP_ONLY : GPIO_FLOATING));
    noGlobalHold();
  }
}

void testTimerAndLight() {
  reset("bounded timer validates and widens before microseconds");
  for(uint32_t ms : {0u, RISC_TIMED_SLEEP_MAX_MS+1, UINT32_MAX}) CHECK(!timerArm(ms));
  CHECK(calls.empty());
  for(uint32_t ms : {1u, RISC_TIMED_SLEEP_MAX_MS}) {
    CHECK(timerArm(ms)); CHECK(timerUs == uint64_t(ms)*1000);
    CHECK(calls.back().arg == uint64_t(ms)*1000);
    CHECK(timerClear()); CHECK(!timerUs);
  }
  CHECK(timerArm(RISC_TIMED_SLEEP_MAX_MS)); CHECK(timerUs == UINT64_C(86400000000));
  errors["enable_timer"] = ESP_FAIL; CHECK(!timerArm(123));
  errors.clear(); CHECK(timerClear());
  for(esp_err_t error : {ESP_OK, ESP_ERR_INVALID_STATE, ESP_FAIL}) {
    errors["disable_timer"] = error; CHECK(timerClear() == (error != ESP_FAIL));
  }
  reset("timer and EXT1 coexist and independently clear");
  CHECK(arm(21,false,true)); CHECK(timerArm(7));
  CHECK(wakeMask == (UINT64_C(1)<<21) && timerUs == 7000);
  CHECK(timerClear()); CHECK(!timerUs && wakeMask);
  CHECK(timerArm(7)); CHECK(clear(21,true)); CHECK(!wakeMask && timerUs == 7000);
  CHECK(timerClear());
  reset("actual Light adapter GPIO plus timer; too-short refusal");
  for(bool high : {false,true}) {
    CHECK(lightArm(7,high)); CHECK(gpioSource);
    CHECK(timerArm(1)); uint32_t cause = RISC_LIGHT_SLEEP_WAKE_NONE;
    errors["light_start"] = ESP_FAIL;
    CHECK(!lightEnter(&cause) && cause == RISC_LIGHT_SLEEP_WAKE_NONE);
    errors.clear(); CHECK(timerClear()); CHECK(lightClear(7));
    CHECK(!gpioSource && !timerUs);
  }
  for(auto cause : {ESP_SLEEP_WAKEUP_GPIO,ESP_SLEEP_WAKEUP_TIMER,ESP_SLEEP_WAKEUP_EXT1}) {
    lightCause=cause; uint32_t result=99; CHECK(lightEnter(&result));
    CHECK(result == (cause==ESP_SLEEP_WAKEUP_GPIO ? RISC_LIGHT_SLEEP_WAKE_GPIO :
      cause==ESP_SLEEP_WAKEUP_TIMER ? RISC_LIGHT_SLEEP_WAKE_TIMER : RISC_LIGHT_SLEEP_WAKE_OTHER));
  }
  for(const char* failure : {"gpio_wake_enable","enable_gpio"}) {
    reset("partial Light arm cleans pin and source"); errors[failure]=ESP_FAIL;
    CHECK(!lightArm(7,false)); errors.clear(); calls.clear();
    CHECK(timerClear()); CHECK(lightClear(7));
    names({"disable_timer","gpio_wake_disable","disable_gpio"});
  }
  for(const char* failure : {"gpio_wake_disable","disable_gpio"}) {
    reset("Light clear attempts both stages"); errors[failure]=ESP_FAIL;
    CHECK(!lightClear(7)); names({"gpio_wake_disable","disable_gpio"});
  }
  reset("already-disabled Light source is safe"); errors["disable_gpio"]=ESP_ERR_INVALID_STATE;
  CHECK(lightClear(7));
}

void testHoldsAndEntry() {
  for (bool enable : {true, false}) {
    reset("per-pin hold wrapper preserves SDK failures");
    pads[45].held = !enable;
    CHECK(hold(45, enable));
    CHECK(pads[45].held == enable);
    CHECK(calls.size() == 1 && calls[0].pin == 45);
    CHECK(calls[0].name == (enable ? "hold_en" : "hold_dis"));
    noGlobalHold();
    reset("failed per-pin hold wrapper does not claim success");
    pads[45].held = !enable;
    errors[enable ? "hold_en" : "hold_dis"] = ESP_FAIL;
    CHECK(!hold(45, enable));
    CHECK(pads[45].held == !enable);
    CHECK(calls.size() == 1 && calls[0].pin == 45);
    CHECK(calls[0].name == (enable ? "hold_en" : "hold_dis"));
    noGlobalHold();
  }
  reset("global digital deep-sleep hold is the last action before entry");
  CHECK(openPin(45, true, true, false));
  CHECK(hold(45, true));
  CHECK(arm(21, false, true));
  noGlobalHold();
  calls.clear();
  bool entered = false;
  try { enter(); } catch (const DeepSleepEntered&) { entered = true; }
  CHECK(entered && globalHold);
  CHECK(pads[45].held && pads[45].level);
  names({"deep_hold_en", "deep_sleep_start"});
}

void testSets(){
  const uint64_t both=(UINT64_C(1)<<14)|(UINT64_C(1)<<21),sensor=UINT64_C(1)<<14,crown=UINT64_C(1)<<21;
  reset("set validation has no SDK mutations");
  CHECK(!setValid(0,0));CHECK(!setValid(1ULL<<22,0));CHECK(!setValid(3,4));CHECK(!setValid(15,3));
  CHECK(!setArm(3,0,4));CHECK(!setClear(3,0,4));CHECK(calls.empty());
  for(uint64_t high:{UINT64_C(0),both,sensor,crown}){
    reset("same and mixed polarity RTC sets");CHECK(setArm(both,high,crown));
    if(high==0 || high==both)CHECK(ext0Pin==-1 && wakeMask==both);
    else CHECK(ext0Pin==(high==sensor?21:14) && ext0Level==0 && wakeMask==high);
    CHECK(power==ESP_PD_OPTION_ON && pads[21].rtcPullup && !pads[14].rtcPullup);
    auto successful=calls;CHECK(timerArm(13));calls.clear();CHECK(setClear(both,high,crown));
    CHECK(!wakeMask && ext0Pin==-1 && timerUs==13000 && power==ESP_PD_OPTION_AUTO);
    CHECK(pads[21].pull==GPIO_PULLUP_ONLY && pads[14].pull==GPIO_FLOATING);
    CHECK(!called("disable_timer") && !called("disable_gpio"));
    const auto cleanup=calls;
    for(size_t i=1;i<=successful.size();++i){reset("every set arm stage fault");failCall=int(i);CHECK(!setArm(both,high,crown));
      CHECK(calls.size()==i);failCall=-1;calls.clear();CHECK(setClear(both,high,crown));CHECK(!wakeMask && ext0Pin==-1 && power==ESP_PD_OPTION_AUTO);
    }
    for(size_t i=1;i<=cleanup.size();++i){reset("every set cleanup stage fault attempts remaining pads");failCall=int(i);CHECK(!setClear(both,high,crown));CHECK(calls.size()==cleanup.size());
      failCall=-1;calls.clear();CHECK(setClear(both,high,crown));CHECK(calls.size()==cleanup.size());
    }
  }
  reset("singleton high routed EXT0 when low group has multiple inputs");
  CHECK(setArm(7,1,6));CHECK(ext0Pin==0 && ext0Level==1 && wakeMask==6 && wakeMode==ESP_EXT1_WAKEUP_ANY_LOW);CHECK(setClear(7,1,6));
  reset("EXT1-only cleanup preserves independently unowned EXT0 and timer");ext0Pin=9;ext0Level=1;timerUs=99;
  CHECK(setClear(both,both,0));CHECK(ext0Pin==9 && timerUs==99 && !called("disable_ext0"));
}
void testStackReadiness() {
  reset("deep-sleep readiness checks current stack before entry");
  pads[45].held = true;
  // Exercise both results repeatedly: the current context must be rechecked.
  for (bool expected : {true, false, true, false}) {
    calls.clear();
    internalStack = expected;
    CHECK(stackReady() == expected);
    names({"ptr_internal"});
    CHECK(calls[0].arg != 0);
    CHECK(pads[45].held && wakeMask == 0 && power == ESP_PD_OPTION_AUTO);
    noGlobalHold();
  }
}
} // namespace

extern "C" {
bool esp_ptr_internal(const void* pointer) {
  CHECK(pointer != nullptr);
  record("ptr_internal", -1, reinterpret_cast<uintptr_t>(pointer));
  return internalStack;
}
bool rtc_gpio_is_valid_gpio(gpio_num_t pin) { return pin >= 0 && pin <= 21; }
bool esp_sleep_is_valid_wakeup_gpio(gpio_num_t pin) { return rtc_gpio_is_valid_gpio(pin); }
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level) {
  const auto result = record("set_level", pin, level);
  if (result == ESP_OK) pads.at(pin).level = level != 0;
  return result;
}
esp_err_t gpio_config(const gpio_config_t* config) {
  CHECK(config && config->pin_bit_mask && !(config->pin_bit_mask & (config->pin_bit_mask - 1)));
  int pin = 0;
  while (!(config->pin_bit_mask & (uint64_t(1) << pin))) ++pin;
  const auto result = record("config", pin);
  calls.back().config = *config;
  if (result == ESP_OK) { pads.at(pin).configured = true; pads.at(pin).config = *config; }
  return result;
}
esp_err_t gpio_set_pull_mode(gpio_num_t pin, gpio_pull_mode_t mode) {
  const auto result = record("digital_pull", pin, mode);
  if (result == ESP_OK) pads.at(pin).pull = mode;
  return result;
}
esp_err_t gpio_hold_en(gpio_num_t pin) {
  const auto result = record("hold_en", pin);
  if (result == ESP_OK) pads.at(pin).held = true;
  return result;
}
esp_err_t gpio_hold_dis(gpio_num_t pin) {
  const auto result = record("hold_dis", pin);
  if (result == ESP_OK) pads.at(pin).held = false;
  return result;
}
esp_err_t rtc_gpio_deinit(gpio_num_t pin) {
  const auto result = record("rtc_deinit", pin);
  if (result == ESP_OK) pads.at(pin).rtc = false;
  return result;
}
esp_err_t rtc_gpio_pullup_en(gpio_num_t pin) {
  const auto result = record("rtc_pullup_en", pin);
  if (result == ESP_OK) pads.at(pin).rtcPullup = true;
  return result;
}
esp_err_t rtc_gpio_pullup_dis(gpio_num_t pin) {
  const auto result = record("rtc_pullup_dis", pin);
  if (result == ESP_OK) pads.at(pin).rtcPullup = false;
  return result;
}
esp_err_t rtc_gpio_pulldown_dis(gpio_num_t pin) {
  const auto result = record("rtc_pulldown_dis", pin);
  if (result == ESP_OK) pads.at(pin).rtcPulldown = false;
  return result;
}
esp_err_t esp_sleep_pd_config(esp_sleep_pd_domain_t domain, esp_sleep_pd_option_t option) {
  const auto result = record("power", domain, option);
  if (result == ESP_OK) power = option;
  return result;
}
esp_err_t esp_sleep_enable_ext0_wakeup(gpio_num_t pin,int level){
  const auto result=record("enable_ext0",pin,level);if(result==ESP_OK){ext0Pin=pin;ext0Level=level;}return result;
}
esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t mask, esp_sleep_ext1_wakeup_mode_t mode) {
  const auto result = record("enable_ext1", mode, mask);
  if (result == ESP_OK) { wakeMask = mask; wakeMode = mode; }
  return result;
}
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t source) {
  CHECK(source == ESP_SLEEP_WAKEUP_TIMER || source == ESP_SLEEP_WAKEUP_EXT1 || source == ESP_SLEEP_WAKEUP_EXT0 || source == ESP_SLEEP_WAKEUP_GPIO);
  const auto result = record(source == ESP_SLEEP_WAKEUP_TIMER ? "disable_timer" :
    source == ESP_SLEEP_WAKEUP_GPIO ? "disable_gpio" : source == ESP_SLEEP_WAKEUP_EXT0 ? "disable_ext0" : "disable_ext1", -1, source);
  if (result == ESP_OK || result == ESP_ERR_INVALID_STATE) {
    if(source == ESP_SLEEP_WAKEUP_TIMER) timerUs=0;
    else if(source == ESP_SLEEP_WAKEUP_GPIO) gpioSource=false;
    else if(source==ESP_SLEEP_WAKEUP_EXT0)ext0Pin=-1;
    else wakeMask=0;
  }
  return result;
}
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us) {
  const auto result=record("enable_timer",-1,us); timerUs=us; return result;
}
esp_err_t gpio_wakeup_enable(gpio_num_t pin,gpio_int_type_t mode) {
  return record("gpio_wake_enable",pin,mode);
}
esp_err_t gpio_wakeup_disable(gpio_num_t pin) { return record("gpio_wake_disable",pin); }
esp_err_t esp_sleep_enable_gpio_wakeup() {
  const auto result=record("enable_gpio"); gpioSource=true; return result;
}
esp_err_t esp_light_sleep_start() { return record("light_start"); }
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return lightCause; }
void gpio_deep_sleep_hold_en() { record("deep_hold_en"); globalHold = true; }
void gpio_deep_sleep_hold_dis() { record("deep_hold_dis"); globalHold = false; }
[[noreturn]] void esp_deep_sleep_start() {
  record("deep_sleep_start");
  CHECK(globalHold);
  throw DeepSleepEntered{};
}
}

int main() {
  testValidity(); testOpen(); testArm(); testClear(); testStackReadiness(); testHoldsAndEntry(); testTimerAndLight(); testSets();
  std::puts("Native sleep SDK shim: actual timer/Light/Deep adapters, 64-bit bounds, both sources, wake causes, every-stage faults, cleanup, stack guard and entry PASS");
}
