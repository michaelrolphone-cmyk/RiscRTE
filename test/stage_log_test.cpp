#include "ports/esp32s3/SleepDiagnostics.h"
#include "diagnostics/Performance.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <cstdio>

using namespace RiscDiagnostics;
static bool owner(){return task==reinterpret_cast<void*>(1);}
static uint64_t clockUs(){return uint64_t(esp_timer_get_time());}
static void fresh(){
  task=reinterpret_cast<void*>(1);Serial=FakeSerial{};start();
  RiscPerf::configure(clockUs,owner,true);timerUs=1234567;timerCalls=0;
}
static void polls(unsigned n=1000){
  for(unsigned i=0;i<n;++i){
    const size_t before=Serial.output.size();poll();assert(Serial.output.size()-before<=64);
  }
}
int main(){
  fresh();
  // Automatic output: neither recorder events nor a command are needed.
  RISC_STAGE_LOG("boot begin reset=%d",1);
  polls();assert(Serial.writes==1 && Serial.output.empty() && Serial.reads==0 && Serial.timeout==0);
  Serial.connected=true;polls();assert(Serial.output=="RTE_STAGE us=1234567 boot begin reset=1\n");
  Serial.output.clear();timerUs=9000000;
  RISC_STAGE_LOG("app load begin file=%s","settings.elf");
  assert(Serial.output=="RTE_STAGE us=9000000 app load begin file=settings.elf\n");
  for(size_t limit:{size_t(1),size_t(7),size_t(63),size_t(256)}){
    Serial.output.clear();Serial.writeLimit=limit;
    const std::string appLine="APP t_ms=9000 stage=draw-begin file="+std::string(150,'a');
    line(appLine.c_str());assert(Serial.output==appLine+"\n");
  }
  // Capacity is checked for the complete line before any fragment is sent.
  Serial.output.clear();Serial.space=15;line("long statement which cannot fit");
  assert(Serial.output.empty());Serial.space=256;Serial.writeLimit=1;polls();
  assert(Serial.output=="RTE_LOG lost=1 truncated=0\n");
  // A stalled short write abandons the payload and repairs its newline before
  // reporting truncation, without buffering old statements.
  Serial.output.clear();Serial.writeLimit=7;Serial.zeroAfter=Serial.writes+1;
  line("a statement which stalls");assert(Serial.output=="a state");
  Serial.zeroAfter=SIZE_MAX;polls();
  assert(Serial.output=="a state\nRTE_LOG lost=1 truncated=1\n");
  Serial.output.clear();Serial.writeLimit=256;
  RISC_STAGE_LOG("subject=%s",std::string(900,'x').c_str());
  assert(Serial.output.size()==256 && Serial.output.substr(252)=="...\n");
  polls();assert(Serial.output.find("RTE_LOG lost=1 truncated=2\n")!=std::string::npos);
  Serial.output.clear();RISC_STAGE_LOG("subject=one\ntwo\rthree");
  assert(Serial.output.find("one two three\n")!=std::string::npos);
  // Wrong owners have no clock or transport effects; reentry cannot splice a
  // statement into the current line or dump.
  const auto calls=Serial.calls;const auto clocks=timerCalls;
  task=reinterpret_cast<void*>(2);RISC_STAGE_LOG("foreign");poll();
  assert(Serial.calls==calls && timerCalls==clocks);task=reinterpret_cast<void*>(1);
  Serial.output.clear();Serial.onWrite=[](){RISC_STAGE_LOG("reentrant");poll();};
  line("outer");assert(Serial.output=="outer\n");polls();
  assert(Serial.output=="outer\nRTE_LOG lost=2 truncated=2\n");
#if RISC_PERFORMANCE_TRACE || RISC_SLEEP_DIAGNOSTICS
  // Existing explicit replay stays framed; live statements and loss notices
  // cannot interleave between its begin and end lines.
  Serial.output.clear();Serial.writeLimit=7;
#if RISC_PERFORMANCE_TRACE
  Serial.input="perf\n";const char* ending="RTE_PERF end\n";
#else
  Serial.input="diag\n";const char* ending="RTE_DIAG end\n";
#endif
  poll();RISC_STAGE_LOG("suppressed-during-replay");polls(10000);
  const auto end=Serial.output.find(ending);assert(end!=std::string::npos);
  assert(Serial.output.substr(0,end).find("\nRTE_STAGE")==std::string::npos);
  assert(Serial.output.substr(0,end).find("RTE_LOG")==std::string::npos);
  assert(Serial.output.find("RTE_LOG lost=3 truncated=2\n")>end);
#endif
  fresh();Serial.connected=true;Serial.writeLimit=0;
  RISC_STAGE_LOG("zero-write");assert(Serial.output.empty());
  Serial.writeLimit=256;polls();assert(Serial.output=="RTE_LOG lost=1 truncated=0\n");
  std::printf("Plain stage logs: automatic timestamps, full app lines, no host, capacity, short/zero writes, loss/truncation, owner/reentry, replay arbitration PASS (diag=%d perf=%d)\n",RISC_SLEEP_DIAGNOSTICS,RISC_PERFORMANCE_TRACE);
}
