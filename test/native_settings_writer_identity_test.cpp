// Real lifecycle prepare/bind with copied-identity mutation and failure controls.
// Native worker scheduling and internal-address predicates remain host fixtures.
#define NATIVE_FIXTURE_EXTERNAL_SOURCE 1
#include "native_replica_checked_fixture.h"
#define private public
#include "bootstrap/Runtime.h"
#undef private
#define main original_settings_launch_fixture_main
#include "native_settings_launch_test.cpp"
#undef main
static bool rejectActivation=false,rejectRetained=false;
int main(int argc,char**argv){
 assert(argc==3);const std::string mode=argv[1];
 F::rootPath=std::string(argv[2])+"/native-store";F::empty();
 auto one=std::make_unique<F::Fixture>(1),six=std::make_unique<F::Fixture>(6);
 F::onWorker=F::lease=false;
 char names[7][96]={"first","second","third","fourth","fifth","sixth","first"};
 L::Writer writers[7]={{names[0],1},{names[1],6},{names[2],1},{names[3],6},{names[4],1},{names[5],6},{names[6],6}};
 if(mode=="duplicate")writers[6].nameSpace=1;
 if(mode=="overflow")std::strcpy(names[6],"seventh");
 if(mode=="long"){std::memset(names[6],'a',95);names[6][95]=0;}
 rejectActivation=mode=="activation";rejectRetained=mode=="retained";
 L::Admission admission{{&one->core,&one->store},{&six->core,&six->store},writers,7,
  &maintenance,sizeof maintenance,
  [](void*,Coordinator&,Coordinator&){return !rejectActivation;},
  [](void*,RiscBoot::Runtime&,RiscCpu::Port&){return L::Progress::Idle;},
  [](void*){return L::Progress::Idle;},
  [](void*){},[](void*){return true;},[](void*){return rejectRetained;}};
 const auto calls=F::nvsTotal+F::fsTotal;
 const bool prepared=L::prepare(&admission,{owner,schedule,[](){return nowMs;},nullptr});
 const bool valid=mode=="valid";
 assert(prepared==valid&&F::nvsTotal+F::fsTotal==calls);
 if(mode=="duplicate"||mode=="overflow"||mode=="long")assert(!services[0]&&!services[1]);
 assert(!L::prepare(&admission,{owner,schedule,[](){return nowMs;},nullptr}));
 RiscBoot::Port port{owner,[](risc_runtime_health_v1*){return true;},schedule,[](const char*){return true;}};
 auto r=std::make_unique<RiscBoot::Runtime>(port);
 if(valid){
  for(auto&name:names)std::strcpy(name,"mutated");
  for(auto&w:writers)w.nameSpace=99;
  assert(L::bind(*r)&&r->atomicBindingCount_==9);
  const char*expected[]={"first","second","third","fourth","fifth","sixth","first"};
  const uint32_t ns[]={1,6,1,6,1,6,6};
  assert(!r->atomicBindings_[0].app[0]&&r->atomicBindings_[0].nameSpace==1);
  assert(!r->atomicBindings_[1].app[0]&&r->atomicBindings_[1].nameSpace==6);
  for(unsigned i=0;i<7;++i){assert(!std::strcmp(r->atomicBindings_[i+2].app,expected[i]));assert(r->atomicBindings_[i+2].nameSpace==ns[i]);}
  assert(!L::bind(*r));
 }else assert(!L::bind(*r)&&!r->atomicBindingCount_);
 assert(F::nvsTotal+F::fsTotal==calls);
 printf("Lifecycle identity mode=%s passed; source mutation cannot change bindings; storage I/O=0\n",mode.c_str());
 fflush(stdout);std::_Exit(0); // Permanent registered clients have no artificial teardown.
}
