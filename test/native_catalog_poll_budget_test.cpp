// Bound the native catalog's collect-and-next scheduling step independently of
// the real Runtime/X4/FatFs everyday-history timing campaign.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main previous_catalog_restore_main
#include "native_checked_sd_restore_test.cpp"
#undef main
#pragma GCC diagnostic pop

struct PollProbe {
 static PollProbe* active;
 NativeSd& fixture;
 decltype(fixture.sd.native.submit) rawSubmit;
 decltype(fixture.sd.native.collect) rawCollect;
 decltype(fixture.sd.download.base.base.submit) submit;
 decltype(fixture.sd.download.base.base.collect) collect;
 unsigned submitted=0,collected=0,fused=0,busy=0,steps=0;
 uint32_t pendingKind=UINT32_MAX,completedKind=UINT32_MAX;
 bool injectBusy=false;
 explicit PollProbe(NativeSd& f,bool inject):fixture(f),rawSubmit(f.sd.native.submit),
   rawCollect(f.sd.native.collect),submit(f.sd.download.base.base.submit),
   collect(f.sd.download.base.base.collect),injectBusy(inject) {
  assert(!active);active=this;
  f.sd.native.submit=[](void*p,const void*q,uint32_t n,uint64_t*t){auto&s=*active;++s.submitted;return s.rawSubmit(p,q,n,t);};
  f.sd.native.collect=[](void*p,uint64_t t,void*q,uint32_t n,uint32_t*z,int32_t*r){auto&s=*active;++s.collected;return s.rawCollect(p,t,q,n,z,r);};
  f.sd.download.base.base.submit=[](void*p,uint64_t session,const risc_storage_async_request_v1*q,uint64_t*t){
   auto&s=*active;
   if(q->operation==RISC_STORAGE_ASYNC_NEXT_ENTRY&&s.completedKind!=UINT32_MAX){
    ++s.fused;
    if(s.injectBusy&&!s.busy){++s.busy;assert(s.fixture.attempt.syncAdmission(55));return int32_t(RISC_STORAGE_ASYNC_BUSY);}
   }
   const int rc=s.submit(p,session,q,t);if(!rc)s.pendingKind=q->operation;return rc;
  };
  f.sd.download.base.base.collect=[](void*p,uint64_t session,uint64_t op,risc_storage_async_result_v1*r){
   auto&s=*active;const int rc=s.collect(p,session,op,r);
   if(rc==RISC_STORAGE_ASYNC_OK&&(s.pendingKind==RISC_STORAGE_ASYNC_OPEN_DIRECTORY||s.pendingKind==RISC_STORAGE_ASYNC_NEXT_ENTRY))s.completedKind=s.pendingKind;
   return rc;
  };
 }
 ~PollProbe(){fixture.sd.native.submit=rawSubmit;fixture.sd.native.collect=rawCollect;fixture.sd.download.base.base.submit=submit;fixture.sd.download.base.base.collect=collect;active=nullptr;}
 void step(){submitted=collected=0;completedKind=UINT32_MAX;++steps;fixture.reader.step();assert(submitted<=1&&collected<=1);}
};
PollProbe* PollProbe::active=nullptr;

static void boundedPolls(){
 for(unsigned mode=0;mode<8;++mode){
  reset();auto card=manyGenerationCard(100);const auto before=card;NativeSd s(card);s.acquire();
  assert(!s.reader.prepareRestoreNative(s.attempt));F::onWorker=true;assert(s.reader.preparationWorkerStep());F::onWorker=false;assert(!s.reader.collectPreparation());
  PollProbe probe(s,mode==1);bool changed=false;
  while(s.reader.state()==K::State::Running){
   const unsigned previous=probe.fused;probe.step();
   if(mode>=2&&!changed&&probe.fused>previous&&probe.submitted){
    changed=true;
    if(mode==2)s.reader.cancel(); // The fused next entry is still queued.
    if(mode==3){s.sdStep();s.reader.cancel();} // Completed but uncollected.
    if(mode==4)++s.epoch;
    if(mode==5)F::lease=false;
    if(mode==6)s.sd.retained=true;
    if(mode==7)s.sd.available=false;
   }
   s.sdStep();assert(probe.steps<10000);
  }
  assert(probe.fused&&card==before&&!s.sd.mutations&&F::nvs.empty());
  Reader::Candidate candidate;
  if(mode<2){assert(!s.reader.takeRestoreCandidate(&candidate)&&candidate.valid()&&s.reader.exitSafe());assert(!s.attempt.release());if(mode==1)assert(probe.busy==1);}
  else {assert(changed&&s.reader.takeRestoreCandidate(&candidate)!=C::Ok&&!candidate.valid());if(mode==2||mode==3){assert(s.reader.state()==K::State::Cancelled&&s.reader.exitSafe());assert(!s.attempt.release());}if(mode==6)assert(s.reader.state()==K::State::Retained&&!s.reader.exitSafe());}
  F::lease=true;F::onWorker=true;
  printf("Native catalog poll budget: mode=%u polls=%u fused_next=%u max_submit=1 max_collect=1 PASS\n",mode,probe.steps,probe.fused);
 }
 reset();
}
int main(int argc,char**argv){assert(argc==2);F::rootPath=argv[1];setbuf(stdout,nullptr);boundedPolls();puts("Native catalog bounded poll suite PASS");}
