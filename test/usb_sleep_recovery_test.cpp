#include "ports/esp32s3/SleepDiagnostics.h"
#include "diagnostics/UsbSleepRecovery.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <iostream>
using namespace RiscDiagnostics;
static void fresh(){task=reinterpret_cast<void*>(1);Serial=FakeSerial{};now=1;start();}
static void recover(){poll();now+=UsbSleepRecovery::DetachMs;poll();}
#if RISC_SLEEP_DIAGNOSTICS
static std::string dump(){Serial.output.clear();Serial.input="diag\n";for(unsigned i=0;i<200;++i){poll();}assert(Serial.output.find("RTE_DIAG end\n")!=std::string::npos);return Serial.output;}
#endif
int main(){
  static_assert(RISC_HWCDC_SLEEP_RECOVERY,"exercise actual hardware-USB selection");
  fresh();
  const auto initial=Serial.calls;
  lightEnter();lightReturn(-1,0);deepEnter();assert(Serial.calls==initial);
  poll();assert(Serial.ends==0 && Serial.begins==0);
  task=reinterpret_cast<void*>(2);const auto foreign=Serial.calls;
  lightReturn(ESP_OK,7);poll();line("wrong-owner");assert(Serial.calls==foreign);
  task=reinterpret_cast<void*>(1);
  const auto before=Serial.calls;lightReturn(ESP_OK,7);lightReturn(ESP_OK,4);
  line("retained-during-recovery");assert(Serial.calls==before);
  poll();assert(Serial.ends==1 && Serial.begins==0);
  const auto detachedCalls=Serial.calls;lightReturn(ESP_OK,7);
  line("detached");for(unsigned i=0;i<100;++i){poll();}
  assert(Serial.calls==detachedCalls && Serial.ends==1 && Serial.begins==0);
  now+=UsbSleepRecovery::DetachMs-1;poll();assert(Serial.begins==0);
  ++now;poll();assert(Serial.begins==1 && Serial.timeout==0);
  for(unsigned i=0;i<100;++i){poll();}assert(Serial.begins==1 && Serial.ends==1 && Serial.writes==0);
  Serial.connected=true;
#if RISC_SLEEP_DIAGNOSTICS
  auto text=dump();assert(text.find("retained-during-recovery")!=std::string::npos);
  assert(text.find("event=light-return detail=0 cause=7")!=std::string::npos);
  assert(text.find("wrong-owner")==std::string::npos);
#endif
  line("awake");assert(Serial.output.find("awake\n")!=std::string::npos);

  // The detach clock begins at the actual owner poll, not the native return.
  fresh();lightReturn(ESP_OK,4);now=UINT32_MAX-10;poll();
  now=8;poll();assert(Serial.begins==0);now=9;poll();assert(Serial.begins==1);

  // Teardown latency does not count toward the host-visible detach dwell.
  fresh();Serial.endDelay=40;lightReturn(ESP_OK,7);poll();assert(now==41);
  now=60;poll();assert(Serial.begins==0);now=61;poll();assert(Serial.begins==1);

  // Every allocation failure remains offline without a busy retry. A later
  // successful sleep permits one new attempt and cleans partial resources.
  for(auto failure:{FakeSerial::Mutex,FakeSerial::Rx,FakeSerial::Tx,FakeSerial::Interrupt}){
    fresh();Serial.failure=failure;lightReturn(ESP_OK,7);recover();assert(Serial.begins==1);
    auto failedCalls=Serial.calls;for(unsigned i=0;i<100;++i){poll();line("offline");}
    assert(Serial.calls==failedCalls && Serial.writes==0);
    lightReturn(-1,0);poll();assert(Serial.begins==1);
    Serial.failure=FakeSerial::None;lightReturn(ESP_OK,7);recover();assert(Serial.begins==2);
    Serial.connected=true;line("recovered");assert(Serial.output=="recovered\n");
  }
#if RISC_SLEEP_DIAGNOSTICS
  // No partial command or interrupted snapshot survives a successful sleep.
  fresh();Serial.connected=true;Serial.input="di";poll();
  lightReturn(ESP_OK,7);recover();Serial.connected=true;Serial.input="ag\n";poll();
  assert(Serial.output.empty());
  Serial.input="diag\n";poll();assert(!Serial.output.empty());
  lightReturn(ESP_OK,7);recover();Serial.connected=true;Serial.output.clear();
  for(unsigned i=0;i<200;++i){poll();}assert(Serial.output.empty());
  text=dump();assert(text.find("event=light-return")!=std::string::npos);
  // A rejected attempt leaves a valid in-flight replay intact.
  Serial.output.clear();Serial.input="diag\n";poll();lightReturn(-1,0);
  for(unsigned i=0;i<200;++i){poll();}assert(Serial.output.find("RTE_DIAG end\n")!=std::string::npos);
#else
  fresh();Serial.connected=true;Serial.input="diag\n";poll();assert(Serial.input=="diag\n" && Serial.output.empty());
#endif
  std::cout<<"USB sleep recovery: RAM-only request, owner polling, bounded detach, repeated sleep, wraparound, no host, allocation failures, no busy retry and recorder mode "<<RISC_SLEEP_DIAGNOSTICS<<" passed\n";
}
