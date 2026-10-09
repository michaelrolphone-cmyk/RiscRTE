#include "ports/esp32s3/SleepDiagnostics.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <cassert>
#include <cstring>
#include <string>
static unsigned observed,drained,reads;
static std::string last;
[[maybe_unused]] static bool recurse;
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
static const risc_diagnostic_source_api_v1* source;
static std::string nativeText="retained snapshot";
static int nativeFault;
static uint64_t nativeSequence=42;
static void readFails(void* context,int32_t expected=RISC_DIAGNOSTIC_SOURCE_INVALID){
 char out[32];std::memset(out,'x',sizeof(out));uint32_t written=99,revision=99;uint64_t sequence=99;
 const auto count=reads;
 assert(source->read(context,0,out,sizeof(out),&written,&sequence,&revision)==expected);
 assert(!written && !sequence && !revision);
 for(char c:out)assert(!c);
 if(expected==RISC_DIAGNOSTIC_SOURCE_INVALID && !nativeFault)assert(reads==count);
}
#endif
#ifndef TEST_ABSENT_OBSERVER
extern "C" void risc_native_diagnostic_observer(const char* text){
 ++observed;last=text;
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
 if(source)readFails(source->context);
#endif
 if(recurse){recurse=false;RiscDiagnostics::line("observer-reentry-must-not-run");}
}
#endif
#ifndef TEST_ABSENT_READ
extern "C" int32_t risc_native_diagnostic_read(uint32_t slot,char* out,uint32_t capacity,
                                               uint32_t* written,uint64_t* sequence,uint32_t* revision){
 ++reads;
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
 assert(slot<RISC_DIAGNOSTIC_SOURCE_MAX_SLOTS && capacity<=RISC_DIAGNOSTIC_SOURCE_TEXT_MAX);
 if(nativeFault==9){readFails(source->context);RiscDiagnostics::line("read-reentry-must-not-run");}
 if(nativeText.size()+1>capacity)return -1;
 std::memcpy(out,nativeText.c_str(),nativeText.size()+1);
 *written=uint32_t(nativeText.size());*sequence=nativeSequence;*revision=7;
 switch(nativeFault){
 case 1:return 0;case 2:return -1;case 3:return 2;
 case 4:*written=capacity;break;case 5:out[*written]='X';break;
 case 6:out[0]=0;break;case 7:*revision=0;break;
 }
 return 1;
#else
 (void)slot;(void)out;(void)capacity;(void)written;(void)sequence;(void)revision;return 0;
#endif
}
#endif
#ifndef TEST_ABSENT_DRAIN
extern "C" void risc_native_diagnostic_drain(void){
 ++drained;
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
 // Source reads must work after OutputGuard unwinds, even without USB space.
 if(source){
   char out[32]{};uint32_t written=0,revision=0;uint64_t sequence=0;
   assert(source->read(source->context,0,out,sizeof(out),&written,&sequence,&revision)==1);
   assert(out==nativeText && written==nativeText.size() && sequence==nativeSequence && revision==7);
 }
 // A broken native drain cannot recursively observe/drain another line.
 RiscDiagnostics::line("drain-reentry-must-not-run");
#endif
}
#endif
static void check(unsigned admitted){
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER && !defined(TEST_ABSENT_OBSERVER)
 assert(observed==admitted);
#else
 assert(observed==0);
#endif
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER && !defined(TEST_ABSENT_DRAIN)
 assert(drained==admitted);
#else
 assert(drained==0);
#endif
 (void)admitted;
}
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
static void sourceChecks(){
 if(!source)return;
 assert(source->api_version==1 && source->struct_size==sizeof(*source) && source->context && source->read);
 const auto serialCalls=Serial.calls;
 char out[RISC_DIAGNOSTIC_SOURCE_TEXT_MAX];uint32_t written,revision;uint64_t sequence;
 auto read=[&](uint32_t slot,uint32_t capacity){return source->read(source->context,slot,out,capacity,&written,&sequence,&revision);};
 assert(read(8,sizeof(out))==1 && std::string(out)==nativeText && written==nativeText.size());
 assert(sequence==42 && revision==7);
 nativeText="changed";assert(std::string(out)=="retained snapshot");
 nativeSequence=0;assert(read(0,sizeof(out))==1 && !sequence && revision==7);
 nativeText.assign(RISC_DIAGNOSTIC_SOURCE_TEXT_MAX-1,'m');
 assert(read(0,sizeof(out))==1 && written==RISC_DIAGNOSTIC_SOURCE_TEXT_MAX-1 && out[written]==0);
 assert(read(0,sizeof(out)-1)==-1 && !out[0] && !written && !sequence && !revision);
 nativeText="retained snapshot";
 auto count=reads;
 for(uint32_t slot:{RISC_DIAGNOSTIC_SOURCE_MAX_SLOTS,UINT32_MAX}){
   assert(read(slot,sizeof(out))==-1 && !written && !sequence && !revision);
 }
 for(uint32_t capacity:{0u,RISC_DIAGNOSTIC_SOURCE_TEXT_MAX+1,UINT32_MAX}){
   assert(read(0,capacity)==-1 && !written && !sequence && !revision);
 }
 assert(reads==count);
 readFails(nullptr);readFails(reinterpret_cast<void*>(1));
 task=reinterpret_cast<void*>(2);readFails(source->context);task=reinterpret_cast<void*>(1);
 assert(source->read(source->context,0,nullptr,sizeof(out),&written,&sequence,&revision)==-1);
 assert(source->read(source->context,0,out,sizeof(out),nullptr,&sequence,&revision)==-1);
 assert(source->read(source->context,0,out,sizeof(out),&written,nullptr,&revision)==-1);
 assert(source->read(source->context,0,out,sizeof(out),&written,&sequence,nullptr)==-1);
 assert(reads==count);
 for(nativeFault=1;nativeFault<=7;++nativeFault)readFails(source->context,nativeFault==1?0:-1);
 nativeFault=9;assert(read(0,sizeof(out))==1);nativeFault=0;
 assert(Serial.calls==serialCalls); // Every source read is RAM-only.
}
#endif
int main(){
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
 source=RiscDiagnostics::nativeSource();
#ifdef TEST_ABSENT_READ
 assert(!source);
#else
 assert(source);readFails(source->context); // start has not established an owner.
#endif
#endif
 RiscDiagnostics::start();Serial.space=0;Serial.connected=false;
 RiscDiagnostics::line("startup-before-USB");check(1);assert(Serial.output.empty());
#if RISC_STAGE_LOGS
 RISC_STAGE_LOG("provider start begin id=panel");check(2);unsigned admitted=2;
#else
 unsigned admitted=1;
#endif
 task=reinterpret_cast<void*>(2);RiscDiagnostics::line("foreign");check(admitted);
 task=reinterpret_cast<void*>(1);RiscDiagnostics::line(nullptr);check(admitted);
 recurse=true;RiscDiagnostics::line("outer");check(++admitted);
 Serial.connected=true;Serial.space=256;Serial.output.clear();
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
 if(source)Serial.onWrite=[](){readFails(source->context);};
#endif
 RiscDiagnostics::line("connected-line");check(++admitted);assert(Serial.output=="connected-line\n");
 Serial.space=0;RiscDiagnostics::line("backpressure");check(++admitted);
 // Poll never invokes a drain, and foreign/null/reentrant lines never do.
 RiscDiagnostics::poll();check(admitted);
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
 sourceChecks();check(admitted);
#else
 assert(!reads);
#endif
#if RISC_HWCDC_SLEEP_RECOVERY
 RiscDiagnostics::lightReturn(0,0);Serial.space=256;Serial.output.clear();
 RiscDiagnostics::line("recovery-suppressed");check(++admitted);assert(Serial.output.empty());
#endif
 return 0;
}
