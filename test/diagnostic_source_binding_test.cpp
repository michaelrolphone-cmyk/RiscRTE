#include "bootstrap/Runtime.h"
#include "ports/esp32s3/SleepDiagnostics.h"
#include <RiscDiagnosticSourceV1.h>
#include <Arduino.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <string>
using RiscBoot::Runtime;
static bool owned=true;
static unsigned starts,apps,reads;
static bool owner(){return owned;}
static bool health(risc_runtime_health_v1*){return true;}
static bool logLine(const char*){return true;}
static void delay(uint32_t){}
extern "C" void test_diagnostic_source_provider_started(){++starts;}
extern "C" void test_diagnostic_source_app_ran(){++apps;}
#ifndef TEST_ABSENT_READ
extern "C" int32_t risc_native_diagnostic_read(uint32_t slot,char* out,uint32_t capacity,uint32_t* written,uint64_t* sequence,uint32_t* revision){
 assert(slot==8 && capacity>=14);++reads;
 std::memcpy(out,"boot snapshot",14);*written=13;*sequence=0;*revision=3;return 1;
}
#endif
static bool bind(Runtime& runtime){
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
 if(const auto* source=RiscDiagnostics::nativeSource())
  return runtime.registerPlatform(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,Runtime::Scope::Global,0,source);
#else
 (void)runtime;
#endif
 return true;
}
static int32_t unusedRead(void*,uint32_t,char*,uint32_t,uint32_t*,uint64_t*,uint32_t*){assert(false);return -1;}
static void registration(){
 const risc_diagnostic_source_api_v1 table={1,sizeof(table),nullptr,unusedRead};
 Runtime runtime({owner,health,delay,logLine});
 for(uint32_t api:{0u,2u})assert(!runtime.registerPlatform(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,api,Runtime::Scope::Global,0,&table));
 for(auto scope:{Runtime::Scope::Device,Runtime::Scope::Bus})assert(!runtime.registerPlatform(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,scope,1,&table));
 assert(!runtime.registerPlatform(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,Runtime::Scope::Global,1,&table));
 auto bad=table;bad.struct_size=8;
 assert(!runtime.registerPlatform(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,Runtime::Scope::Global,0,&bad));
 bad=table;bad.read=nullptr;
 assert(!runtime.registerPlatform(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,Runtime::Scope::Global,0,&bad));
 bad=table;bad.api_version=2;
 assert(!runtime.registerPlatform(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,Runtime::Scope::Global,0,&bad));
 assert(!runtime.registerPlatform("platform.diagnostic-source.other",1,Runtime::Scope::Global,0,&table));
 owned=false;assert(!runtime.registerPlatform(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,Runtime::Scope::Global,0,&table));owned=true;
 assert(runtime.registerPlatform(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,Runtime::Scope::Global,0,&table));
 assert(!runtime.registerPlatform(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,Runtime::Scope::Global,0,&table));
}
int main(int argc,char** argv){
 assert(argc==2);const std::string root=argv[1];
 auto file=[&](const char* name,const std::string& value){std::ofstream(root+"/"+name)<<value;};
 auto stage=[&](const char* capability="test.diagnostic-consumer"){
  file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
  file("provider.json",R"({"type":"driver","id":"diagnostic-consumer","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"provider.elf","requires":[{"capability":"platform.diagnostic-source","api":1}],"provides":[{"capability":"test.diagnostic-consumer","api":1}]})");
  file("app.json",std::string(R"({"type":"application","id":"diagnostic-source-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":")")+capability+R"(","api":1}]})");
  file("boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"provider.json"}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":")")+capability+R"(","api":1,"instance_id":0}]}]})");
 };
 registration();RiscDiagnostics::start();stage();
 {Runtime runtime({owner,health,delay,logLine});assert(!runtime.prepare(root.c_str()));assert(!std::strcmp(runtime.error(),"missing scoped trusted platform provider"));}
 {Runtime runtime({owner,health,delay,logLine,bind});
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER && !defined(TEST_ABSENT_READ)
  assert(runtime.prepare(root.c_str()) && !reads && !starts && !apps);
  assert(runtime.run() && reads==1 && starts==1 && apps==1);
  assert(!runtime.registerPlatform(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,Runtime::Scope::Global,0,RiscDiagnostics::nativeSource()));
#else
  assert(!runtime.prepare(root.c_str()) && !reads && !starts && !apps);
#endif
 }
 // An explicit app declaration and grant still cannot expose a raw source.
 stage(RISC_DIAGNOSTIC_SOURCE_CAPABILITY);
 JsonDocument config;assert(RiscBoot::readJson((root+"/boot.json").c_str(),config));config["drivers"].to<JsonArray>();
 std::string text;serializeJson(config,text);file("boot.json",text);
 {Runtime runtime({owner,health,delay,logLine,bind});assert(!runtime.prepare(root.c_str()));assert(!std::strcmp(runtime.error(),"raw platform capability denied to app"));}
 puts("Diagnostic source binding: exact global v1, real provider dependency, absent/off rejection, explicit and live app denial PASS");
}
