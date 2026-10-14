// Focused production Runtime cohort boundary. Payload admission below is an
// inert fixture; final product qualification separately admits real target ELFs.
#define NATIVE_ATOMIC_ENDPOINT_FIXTURE_ONLY
#include "native_atomic_key_value_test.cpp"
#define private public
#include "bootstrap/Runtime.h"
#undef private
#include <fstream>
#include <filesystem>
using R=RiscBoot::Runtime;
static A*one;static A*six;static bool retainedOne=false,retainedSix=false;
static unsigned bindings=0,availabilityCalls=0,admissions=0;
static std::string root;
static bool owner(){return true;}
static bool health(risc_runtime_health_v1*){return true;}
static void delay(uint32_t){}
static bool logLine(const char*){return true;}
static int32_t availability(void*p,uint32_t ns,risc_key_value_availability_state_v1*){assert(uintptr_t(p)==ns);++availabilityCalls;return RISC_KEY_VALUE_AVAILABILITY_CONTEXT;}
static bool bind(R&r){
 ++bindings;
 for(A*e:{one,six}){const auto ns=e->namespaceId();assert(r.registerAtomicKeyValueReadThrough(ns,*e));assert(r.registerKeyValueAvailability(ns,{reinterpret_cast<void*>(uintptr_t(ns)),availability}));}
 for(const char*name:{"settings","paper_clock","battery","ble_scanner","contexts"})assert(r.registerAtomicKeyValue(name,1,*one));
 assert(r.registerAtomicKeyValue("wifi_settings",6,*six));return true;
}
static const char*names[]={"paper_clock","settings","battery","ble_scanner","contexts","wifi_settings"};
static void file(const std::string&name,const std::string&data){std::ofstream(root+"/"+name)<<data;}
static void store(){
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 file("cohort.json","{}");
 std::string boot=R"({"board":"board.json","default_app":"paper_clock.elf","drivers":[],"app_capabilities":[)";
 for(unsigned i=0;i<6;++i){const std::string id=names[i],ns=i==5?"6":"1";
  file(id+".elf","inert admission payload");
  file(id+".json","{\"type\":\"application\",\"id\":\""+id+"\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"file_name\":\""+id+".elf\",\"entry\":\"app_main\",\"requires\":[{\"capability\":\"storage.key-value.atomic\",\"api\":1},{\"capability\":\"storage.key-value.availability\",\"api\":1}]}");
  if(i)boot+=',';boot+="{\"manifest\":\""+id+".json\",\"grants\":[{\"capability\":\"storage.key-value.atomic\",\"api\":1,\"instance_id\":"+ns+"},{\"capability\":\"storage.key-value.availability\",\"api\":1,\"instance_id\":"+ns+"}]}";
 }
 file("boot.json",boot+"]}");
}
static bool admit(void*,const char*p,bool driver){assert(!driver&&std::filesystem::is_regular_file(p));++admissions;return true;}
static void same(const R&from,const R&to){
 assert(from.atomicBindingCount_==8&&to.atomicBindingCount_==8);
 for(unsigned i=0;i<8;++i){const auto&a=from.atomicBindings_[i];const auto&b=to.atomicBindings_[i];
  assert(!strcmp(a.app,b.app)&&a.nameSpace==b.nameSpace&&a.endpoint==b.endpoint&&a.availability.context==b.availability.context&&a.availability.snapshot==b.availability.snapshot);
 }
 for(const auto&g:to.mainInvocation_.appGrants_)assert(!g.live);
 assert(!to.foreground_&&!to.mainInvocation_.active_);
}
int main(int argc,char**argv){
 assert(argc==2);root=argv[1];std::filesystem::create_directories(root);store();
 Disk d1,d6;Alloc allocation;C c1(1,d1.backend(),limits(),allocation.api()),c6(6,d6.backend(),limits(),allocation.api());assert(!c1.initialize(nullptr,0,{})&&!c6.initialize(nullptr,0,{}));
 Gate g1,g6;auto guard1=g1.api(),guard6=g6.api();guard1.retained=[](void*){return retainedOne;};guard6.retained=[](void*){return retainedSix;};A a1(c1,guard1),a6(c6,guard6);one=&a1;six=&a6;
 R source({owner,health,delay,logLine,bind});assert(source.prepare(root.c_str()));assert(bindings==1&&!availabilityCalls);d1.calls=d6.calls=0;
 {R candidate({});assert(source.validateCohort(candidate,root.c_str(),admit,nullptr));same(source,candidate);assert(admissions==6&&bindings==1&&!availabilityCalls&&!d1.calls&&!d6.calls);}
 // A poisoned/preseeded candidate cannot supplement the trusted source rows.
 {R candidate({owner,health,delay,logLine});assert(candidate.registerAtomicKeyValue("intruder",1,a1));assert(!source.validateCohort(candidate,root.c_str(),admit,nullptr));assert(candidate.atomicBindingCount_==1&&!candidate.attempted_);}
 for(unsigned ns:{1u,6u}){
  auto&e=ns==1?a1:a6;auto&c=ns==1?c1:c6;auto&g=ns==1?g1:g6;
  const auto token=submit(e,c);const auto before=d1.calls+d6.calls;const auto count=admissions;
  {R candidate({});assert(!source.validateCohort(candidate,root.c_str(),admit,nullptr));assert(!candidate.attempted_&&admissions==count&&d1.calls+d6.calls==before);}
  assert(e.cancel(token)==RISC_ATOMIC_PENDING);g.run(e);assert(collect(e,token).status==RISC_ATOMIC_CANCELLED&&e.exitSafe());
 }
 for(bool*flag:{&retainedOne,&retainedSix}){
  *flag=true;const auto count=admissions;
  {R candidate({});assert(!source.validateCohort(candidate,root.c_str(),admit,nullptr));assert(!candidate.attempted_&&admissions==count);}
  *flag=false;
 }
 {R candidate({});assert(source.validateCohort(candidate,root.c_str(),admit,nullptr));same(source,candidate);}
 // Normal clean collection permits a fresh candidate; no availability callback,
 // platform rebind, worker or native disk mutation occurs during admission.
 assert(bindings==1&&!availabilityCalls&&!d1.calls&&!d6.calls&&admissions==12);
 puts("Atomic staged cohort: eight exact bindings, both namespace callbacks, six existing IDs; no active contexts/rebind/I/O; preseed, pending and retained refusal; clean retry PASS");
}
