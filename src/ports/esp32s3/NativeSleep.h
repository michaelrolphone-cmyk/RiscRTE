#pragma once
/* Generic IDF4.4.7 adapter, shared with the native SDK-shim regression.
 * There is one CPU sleep-configuration owner in this minimal runtime. Its
 * baseline RTC_PERIPH policy is AUTO; no other wake/configuration owner exists.
 */
#include <cstdint>
#include "SleepDiagnostics.h"
#include <RiscTimedSleepV1.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <soc/soc_memory_types.h>
namespace RiscCpu { namespace NativeSleep {
inline bool timerArm(uint32_t ms) {
  if(!ms || ms>RISC_TIMED_SLEEP_MAX_MS)return false;
  return esp_sleep_enable_timer_wakeup(uint64_t(ms)*UINT64_C(1000))==ESP_OK;
}
inline bool timerClear() {
  const esp_err_t result=esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  return result==ESP_OK || result==ESP_ERR_INVALID_STATE;
}
inline uint32_t lightWakeCause(esp_sleep_wakeup_cause_t cause) {
  if(cause==ESP_SLEEP_WAKEUP_GPIO)return RISC_LIGHT_SLEEP_WAKE_GPIO;
  if(cause==ESP_SLEEP_WAKEUP_TIMER)return RISC_LIGHT_SLEEP_WAKE_TIMER;
  return RISC_LIGHT_SLEEP_WAKE_OTHER;
}
inline bool lightArm(uint8_t pin,bool active) {
  return gpio_wakeup_enable(static_cast<gpio_num_t>(pin),active?GPIO_INTR_HIGH_LEVEL:GPIO_INTR_LOW_LEVEL)==ESP_OK &&
    esp_sleep_enable_gpio_wakeup()==ESP_OK;
}
inline bool lightClear(uint8_t pin) {
  const bool pinOk=gpio_wakeup_disable(static_cast<gpio_num_t>(pin))==ESP_OK;
  const esp_err_t cleared=esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  const bool sourceOk=cleared==ESP_OK || cleared==ESP_ERR_INVALID_STATE;
  return pinOk && sourceOk;
}
inline bool lightEnter(uint32_t* cause) {
  RiscDiagnostics::lightEnter();
  const esp_err_t result=esp_light_sleep_start();
  const auto wake=result==ESP_OK?esp_sleep_get_wakeup_cause():ESP_SLEEP_WAKEUP_UNDEFINED;
  RiscDiagnostics::lightReturn(result,uint32_t(wake));
  if(result!=ESP_OK)return false;
  *cause=lightWakeCause(wake);
  return true;
}
inline bool stackReady() {uint8_t probe=0;return esp_ptr_internal(&probe);}
inline bool* failedOpens() {static bool failed[GPIO_NUM_MAX]{};return failed;}
inline bool canClose(uint8_t pin) {return pin<GPIO_NUM_MAX && !failedOpens()[pin];}
inline bool openPin(uint8_t pin,bool output,bool initial,bool pullup) {
  const auto gpio=static_cast<gpio_num_t>(pin);
  if(pin>=GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(pin) || (output && !GPIO_IS_VALID_OUTPUT_GPIO(pin)))return false;
  // gpio_reset_pin reports success even with an active pad hold. A failed open
  // therefore cannot be reported clean by a later generic close operation.
  failedOpens()[pin]=true;
  // A deep-wake input can still be routed to RTC. Per-pad holds remain engaged
  // while the requested digital initial state is staged, preventing a glitch.
  if(rtc_gpio_is_valid_gpio(gpio) && rtc_gpio_deinit(gpio)!=ESP_OK)return false;
  if(output && gpio_set_level(gpio,initial)!=ESP_OK)return false;
  gpio_config_t config{};config.pin_bit_mask=uint64_t(1)<<pin;
  config.mode=output?GPIO_MODE_OUTPUT:GPIO_MODE_INPUT;
  config.pull_up_en=pullup?GPIO_PULLUP_ENABLE:GPIO_PULLUP_DISABLE;
  config.pull_down_en=GPIO_PULLDOWN_DISABLE;config.intr_type=GPIO_INTR_DISABLE;
  if(gpio_config(&config)!=ESP_OK)return false;
  // IDF requires known configuration BEFORE unhold after a deep-sleep reset.
  if(GPIO_IS_VALID_OUTPUT_GPIO(pin) && gpio_hold_dis(gpio)!=ESP_OK)return false;
  failedOpens()[pin]=false;return true;
}
inline bool valid(uint8_t pin) {
  return pin<GPIO_NUM_MAX && GPIO_IS_VALID_GPIO(pin) && esp_sleep_is_valid_wakeup_gpio(static_cast<gpio_num_t>(pin));
}
inline bool arm(uint8_t pin,bool active,bool pullup) {
  const auto gpio=static_cast<gpio_num_t>(pin);
  // IDF4 ext1_wakeup_prepare disables both pulls when RTC_PERIPH powers down.
  // Preserve a pull requested by the actual input claim, not merely its scope.
  if(esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,pullup?ESP_PD_OPTION_ON:ESP_PD_OPTION_AUTO)!=ESP_OK ||
     rtc_gpio_pulldown_dis(gpio)!=ESP_OK ||
     (pullup?rtc_gpio_pullup_en(gpio):rtc_gpio_pullup_dis(gpio))!=ESP_OK)return false;
  return esp_sleep_enable_ext1_wakeup(uint64_t(1)<<pin,
    active?ESP_EXT1_WAKEUP_ANY_HIGH:ESP_EXT1_WAKEUP_ANY_LOW)==ESP_OK;
}
inline bool clear(uint8_t pin,bool pullup) {
  const auto gpio=static_cast<gpio_num_t>(pin);
  const esp_err_t disabled=esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
  bool ok=disabled==ESP_OK || disabled==ESP_ERR_INVALID_STATE;
  // Attempt every cleanup operation even after an earlier failure.
  if(rtc_gpio_pullup_dis(gpio)!=ESP_OK)ok=false;
  if(rtc_gpio_pulldown_dis(gpio)!=ESP_OK)ok=false;
  if(rtc_gpio_deinit(gpio)!=ESP_OK)ok=false;
  if(gpio_set_pull_mode(gpio,pullup?GPIO_PULLUP_ONLY:GPIO_FLOATING)!=ESP_OK)ok=false;
  if(esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,ESP_PD_OPTION_AUTO)!=ESP_OK)ok=false;
  return ok;
}
/* EXT1 has one polarity on the pinned S3 SDK. For a mixed set, use EXT0
 * for a singleton polarity and EXT1 for the other. No peripheral identity is
 * involved. The exclusive sleep owner never touches sources outside this plan. */
struct WakePlan { uint64_t ext0=0,ext1=0; bool ext0High=false,ext1High=false; };
inline bool plan(uint64_t mask,uint64_t high,WakePlan& p){
  p={};if(!mask || (high&~mask) || (mask>>49))return false;
  for(unsigned i=0;i<49;++i)if((mask&(UINT64_C(1)<<i)) && !valid(i))return false;
  const uint64_t low=mask&~high;
  if(!low || !high){p.ext1=mask;p.ext1High=high!=0;return true;}
  if(!(low&(low-1))){p.ext0=low;p.ext1=high;p.ext1High=true;return true;}
  if(!(high&(high-1))){p.ext0=high;p.ext0High=true;p.ext1=low;return true;}
  return false;
}
inline bool setValid(uint64_t mask,uint64_t high){WakePlan p;return plan(mask,high,p);}
inline bool setArm(uint64_t mask,uint64_t high,uint64_t pullups){
  WakePlan p;if((pullups&~mask) || !plan(mask,high,p))return false;
  if(esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,(pullups || p.ext0)?ESP_PD_OPTION_ON:ESP_PD_OPTION_AUTO)!=ESP_OK)return false;
  for(unsigned i=0;i<49;++i)if(mask&(UINT64_C(1)<<i)){
    const auto gpio=static_cast<gpio_num_t>(i);
    if(rtc_gpio_pulldown_dis(gpio)!=ESP_OK ||
       ((pullups&(UINT64_C(1)<<i))?rtc_gpio_pullup_en(gpio):rtc_gpio_pullup_dis(gpio))!=ESP_OK)return false;
  }
  if(p.ext0){unsigned pin=0;while(!(p.ext0&(UINT64_C(1)<<pin)))++pin;
    if(esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(pin),p.ext0High?1:0)!=ESP_OK)return false;
  }
  return esp_sleep_enable_ext1_wakeup(p.ext1,p.ext1High?ESP_EXT1_WAKEUP_ANY_HIGH:ESP_EXT1_WAKEUP_ANY_LOW)==ESP_OK;
}
inline bool setClear(uint64_t mask,uint64_t high,uint64_t pullups){
  WakePlan p;if((pullups&~mask) || !plan(mask,high,p))return false;
  bool ok=true;
  // Each planned source may have been partially armed, including a failed call.
  // Never clear EXT0 on an EXT1-only operation, nor unrelated TIMER/GPIO/ALL.
  if(p.ext0){const auto rc=esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);if(rc!=ESP_OK && rc!=ESP_ERR_INVALID_STATE)ok=false;}
  if(p.ext1){const auto rc=esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);if(rc!=ESP_OK && rc!=ESP_ERR_INVALID_STATE)ok=false;}
  for(unsigned i=0;i<49;++i)if(mask&(UINT64_C(1)<<i)){
    const auto gpio=static_cast<gpio_num_t>(i);
    if(rtc_gpio_pullup_dis(gpio)!=ESP_OK)ok=false;
    if(rtc_gpio_pulldown_dis(gpio)!=ESP_OK)ok=false;
    if(rtc_gpio_deinit(gpio)!=ESP_OK)ok=false;
    if(gpio_set_pull_mode(gpio,(pullups&(UINT64_C(1)<<i))?GPIO_PULLUP_ONLY:GPIO_FLOATING)!=ESP_OK)ok=false;
  }
  if(esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,ESP_PD_OPTION_AUTO)!=ESP_OK)ok=false;
  return ok;
}
inline bool hold(uint8_t pin,bool enable) {
  const auto gpio=static_cast<gpio_num_t>(pin);
  return (enable?gpio_hold_en(gpio):gpio_hold_dis(gpio))==ESP_OK;
}
inline void enter() {
  // This affects digital pads only while deeply asleep, unlike force_hold_all
  // which would immediately freeze flash/UART and cannot run from flash code.
  RiscDiagnostics::deepEnter();
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();
}
}}
