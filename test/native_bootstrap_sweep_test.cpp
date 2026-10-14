// Reuse the real X4/FatFs/Runtime/native-store fixture without its case main.
// The runner copies only that fixture prefix; its source bytes are recorded.
#include NATIVE_BOOTSTRAP_SWEEP_FIXTURE
int main(int argc,char**argv){
 assert(argc==4);scenario=argv[2];F::rootPath=argv[3];F::empty();
 const bool firstSucceeds=scenario=="both-ready"||scenario=="conflict-second";
 if(firstSucceeds)F::nvs["rte00000001"]["setting"]={'f','i','r','s','t'};
 if(scenario=="conflict-second")F::nvs["kvj00000006"]["unknown"]={1};
 F::nvs["rte00000006"]["setting"]={'s','i','b','l','i','n','g'};
 if(scenario=="conflict-first")F::nvs["kvj00000001"]["unknown"]={1};
 const auto originals=F::nvs;
 auto*a=new F::Fixture(1);auto*b=new F::Fixture(6);cores[0]=&a->core;cores[1]=&b->core;
 Hash h1,h6;const auto d1=h1.api(),d6=h6.api();
 auto*s1=new Sources(1,nativeOwner,a->inspector,sourceHooks(*a));auto*s6=new Sources(6,nativeOwner,b->inspector,sourceHooks(*b));
 auto*l1=new LegacySources(1,nativeOwner,"shared-00000001",S::ImageMax-C::HeaderV3-4,a->inspector,{d1.context,d1.begin,d1.update,d1.finish},legacyHooks(*a));
 auto*l6=new LegacySources(6,nativeOwner,"shared-00000006",S::ImageMax-C::HeaderV3-4,b->inspector,{d6.context,d6.begin,d6.update,d6.finish},legacyHooks(*b));
 auto*client=new B({&a->core,&a->store,d1,C::heap(),scenario=="missing-source-first"?nullptr:s1,{},l1},{&b->core,&b->store,d6,C::heap(),s6,{},l6},9);
 static const L::Writer writers[]={{"maintenance-test",1},{"maintenance-test",6}};
 maintenance=new M({&a->core,&a->store,d1,C::heap()},{&b->core,&b->store,d6,C::heap()},9,writers,2,client);
 F::onWorker=false;F::lease=false;
 for(unsigned i=0;i<2;++i){const unsigned ns=i?6:1;F::Fixture&native=i?*b:*a;
  services[i]=std::make_unique<Coordinator>(native.core,native.store,Coordinator::Hooks{reinterpret_cast<void*>(uintptr_t(ns)),
   [](void*){return owner();},[](void*p){return task==Task::NativeSettings&&onNamespace==uintptr_t(p);},
   [](void*p){return !(scenario=="native-busy-first"&&uintptr_t(p)==1)&&!F::lease.exchange(true);},
   [](void*){assert(F::lease.exchange(false));},[](void*,const void*p,size_t n){return p&&n;}});
 }
 const auto&admission=maintenance->admission();assert(admission.sourceStatus);
 assert(admission.registerBeforeActivate(admission.context,*services[0],*services[1]));
 for(auto&s:services)assert(!s->activate());
 Port cpu(hardware());port=&cpu;RiscBoot::Port boot{owner,[](risc_runtime_health_v1*){return true;},schedule,[](const char*){return true;},bind};
 static const RiscBoot::KeyValueBackend unusedLegacy{nullptr,[](void*,uint32_t,const char*,void*,uint32_t,uint32_t*){return RISC_KEY_VALUE_IO;},[](void*,uint32_t,const char*,const void*,uint32_t){return RISC_KEY_VALUE_IO;},2048};boot.keyValue=&unusedLegacy;
 RiscBoot::Runtime runtime(boot);
 for(unsigned i=0;i<2;++i){assert(runtime.registerAtomicKeyValueReadThrough(i?6:1,services[i]->endpoint()));assert(runtime.registerAtomicKeyValue("maintenance-test",i?6:1,services[i]->endpoint()));}
 if(!runtime.prepare(argv[1])){fprintf(stderr,"prepare: %s\n",runtime.error());assert(false);}Pin anchor(runtime,cpu,9);anchorPin=&anchor;assert(!anchor.acquire());
 if(scenario=="defer-first")f->fault(8);
 else {P initial(runtime,cpu,1,9);bootstrap(initial);assert(!initial.release());}
 if(scenario=="retained-stop")f->fault(3);
 auto progress=admission.beginPreApp(admission.context,runtime,cpu,C::Missing,C::Missing);
 if(scenario=="cancel-stop")admission.cancel(admission.context);
 for(unsigned n=0;progress==L::Progress::Pending&&n<20000;++n){schedule(1);progress=admission.poll(admission.context);}
 if(scenario=="retained-stop"){
  assert(progress==L::Progress::Retained&&admission.retained(admission.context)&&!admission.exitSafe(admission.context)&&!b->core.ready());
  puts("retained discovery stops bounded sweep and preserves sibling/source controls PASS");std::fflush(stdout);std::_Exit(0);
 }
 assert(progress!=L::Progress::Pending&&admission.exitSafe(admission.context)&&!admission.retained(admission.context)&&!cpu.nativeStorageAttached());
 assert(a->core.ready()==firstSucceeds&&admission.sourceStatus(admission.context,3)==L::Progress::Failed);
 if(firstSucceeds){
  const bool both=scenario=="both-ready";const auto expected=both?L::Progress::Idle:L::Progress::Failed;
  assert(F::value(a->core,"setting")=="first"&&b->core.ready()==both&&progress==expected);
  assert(admission.sourceStatus(admission.context,1)==L::Progress::Idle&&admission.sourceStatus(admission.context,6)==expected);
  for(const auto&space:originals)assert(F::nvs.at(space.first)==space.second);
  const auto revision=a->core.revision();progress=admission.beginPreApp(admission.context,runtime,cpu,C::Ok,both?C::Ok:C::Missing);
  for(unsigned n=0;progress==L::Progress::Pending&&n<20000;++n){schedule(1);progress=admission.poll(admission.context);}
  assert(progress==expected&&a->core.revision()==revision);
 }else if(scenario=="cancel-stop"){
  assert(progress==L::Progress::Deferred&&!b->core.ready()&&F::nvs==originals);
  assert(admission.sourceStatus(admission.context,1)==L::Progress::Deferred&&admission.sourceStatus(admission.context,6)==L::Progress::Deferred);
 }else {
  assert(b->core.ready()&&F::value(b->core,"setting")=="sibling"&&b->core.baseline().archiveObligation());
  for(const auto&space:originals)assert(F::nvs.at(space.first)==space.second);
  const auto expected=scenario=="conflict-first"?L::Progress::Failed:L::Progress::Deferred;
  assert(progress==expected&&admission.sourceStatus(admission.context,1)==expected&&admission.sourceStatus(admission.context,6)==L::Progress::Idle);
  const auto revision=b->core.revision();
  progress=admission.beginPreApp(admission.context,runtime,cpu,C::Missing,C::Ok);
  for(unsigned n=0;progress==L::Progress::Pending&&n<20000;++n){schedule(1);progress=admission.poll(admission.context);}
  assert(progress==expected&&b->core.revision()==revision&&admission.sourceStatus(admission.context,6)==L::Progress::Idle);
 }
 assert(F::handles.empty()&&iterators.empty()&&!anchor.release()&&!f);
 printf("checked bootstrap bounded sweep %s: exact per-namespace status and no sibling starvation PASS\n",scenario.c_str());std::fflush(stdout);std::_Exit(0);
}
