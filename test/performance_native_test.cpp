#include "ports/esp32s3/SleepDiagnostics.h"
#include "diagnostics/Performance.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <cstdio>
using namespace RiscDiagnostics;
static uint64_t tick=0;
static bool owner(){return task==reinterpret_cast<void*>(1);}
static uint64_t clockUs(){return ++tick;}
static void step(){
  const auto bytes=Serial.output.size(),writes=Serial.writes,reads=Serial.reads;
  poll();
  assert(Serial.output.size()-bytes<=64 && Serial.writes-writes<=1 && Serial.reads-reads<=16);
}
static std::string dump(const char* command,const char* ending){
  Serial.output.clear();Serial.input=command;
  for(unsigned i=0;i<10000 && Serial.output.find(ending)==std::string::npos;++i)step();
  assert(Serial.output.find(ending)!=std::string::npos);return Serial.output;
}
static void fresh(){
  task=reinterpret_cast<void*>(1);Serial=FakeSerial{};now=1;
  start();RiscPerf::configure(clockUs,owner,true);
  RiscPerf::invocation("default.elf");RiscPerf::interaction(0,1,3);RiscPerf::emit(4,42);
}
int main(){
  fresh();assert(Serial.timeout==0);
  Serial.input="perf\n";
  for(unsigned i=0;i<30;++i)step();
  assert(!Serial.writes && !Serial.reads);
  Serial.connected=true;
  const std::string expected=dump("perf\n","RTE_PERF end\n");
  assert(expected.find("schema=1 records=3")!=std::string::npos);
  assert(expected.find("interaction=1 invocation=1 phase=4 value=42")!=std::string::npos);
  for(size_t limit:{size_t(1),size_t(7),size_t(63)}){
    Serial.writeLimit=limit;
    assert(dump("perf\n","RTE_PERF end\n")==expected);
  }
  Serial.writeLimit=256;Serial.space=0;Serial.output.clear();Serial.input="perf\n";
  const auto before=Serial.writes;step();assert(Serial.writes==before);
  line("must-not-interleave");assert(Serial.output.empty());
  Serial.space=256;Serial.writeLimit=0;step();assert(Serial.output.empty());
  Serial.writeLimit=256;
  for(unsigned i=0;i<500;++i)step();assert(Serial.output==expected);
#if RISC_SLEEP_DIAGNOSTICS
  // Queued diag/perf commands cannot start concurrent producers; every poll
  // obeys one shared 64-byte transport budget regardless of command order.
  for(const char* input:{"diag\nperf\n","perf\ndiag\n"}){
    Serial.output.clear();Serial.input=input;
    for(unsigned i=0;i<500;++i)step();
    if(input[0]=='d')assert(Serial.output.find("RTE_DIAG end\n")!=std::string::npos && Serial.output.find("RTE_PERF")==std::string::npos);
    else assert(Serial.output.find("RTE_PERF end\n")!=std::string::npos && Serial.output.find("RTE_DIAG")==std::string::npos);
  }
  assert(dump("diag\n","RTE_DIAG end\n").find("event=boot-fresh")!=std::string::npos);
#else
  assert(dump("diag\nperf\n","RTE_PERF end\n")==expected);
#endif
  Serial.input="perf\n";task=reinterpret_cast<void*>(2);const auto calls=Serial.calls;
  step();line("foreign");lightReturn(ESP_OK,0);assert(Serial.calls==calls && Serial.input=="perf\n");
  task=reinterpret_cast<void*>(1);step();
  Serial.connected=false;step();Serial.connected=true;Serial.output.clear();
  for(unsigned i=0;i<500;++i)step();assert(Serial.output.empty());
  // Disconnect clears incomplete commands as well as an active snapshot.
  Serial.input="pe";step();Serial.connected=false;step();Serial.connected=true;
  Serial.input="rf\n";step();assert(Serial.output.empty());
  assert(dump("perf\n","RTE_PERF end\n")==expected);
  // Successful wake invalidates a snapshot and partial command before USB
  // recovery. No stale tail is replayed after reconnection.
  Serial.input="perf\n";step();lightReturn(ESP_OK,3);step();now+=20;step();
  Serial.connected=true;Serial.output.clear();
  for(unsigned i=0;i<500;++i)step();assert(Serial.output.empty());
  assert(dump("perf\n","RTE_PERF end\n")==expected);
  Serial.output.clear();Serial.input="pe";step();lightReturn(ESP_OK,3);step();now+=20;step();
  Serial.connected=true;Serial.input="rf\n";step();assert(Serial.output.empty());
#if RISC_SLEEP_DIAGNOSTICS
  assert(dump("diag\n","RTE_DIAG end\n").find("event=light-return")!=std::string::npos);
#endif
  std::printf("Performance native adapter (diagnostics=%d): absent host, ownership, exact short-write replay, backpressure, shared 64-byte poll budget, diagnostics, disconnect and wake recovery PASS\n",RISC_SLEEP_DIAGNOSTICS);
}
