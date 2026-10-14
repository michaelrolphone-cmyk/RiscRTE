#include <experimental/native_replica/NativeRebaseCandidate.h>
#include <experimental/wifi_reconcile/WifiStage.h>
using R=RiscExperimental::NativeRebaseCandidate;
using S=RiscWifiExperimental::Stage;
extern "C" int experimentalWifiRebaseCompile(R*receiver,S*stage,const S::View*view,uint64_t id,const RiscWifiExperimental::Reconcile::Intent*intent){
 auto result=stage->begin(*view,id,*intent);
 result=stage->step();
 if(result==S::Ready)result=stage->commit({receiver,[](void*p,const S::Batch*b){
  if(!b||b->count>S::MaxMutations)return S::CommitConflict;
  RiscExperimental::NativeReplicaCore::Mutation changes[S::MaxMutations]{};
  for(uint32_t i=0;i<b->count;++i)changes[i]={b->mutations[i].key,RiscExperimental::NativeReplicaCore::Put,b->mutations[i].value,b->mutations[i].size};
  const R::Batch batch{b->namespaceId,b->viewRevision,b->pendingFrontier,b->originalIntentId,b->originalIntent.bytes,sizeof b->originalIntent.bytes,changes,b->count};
  const int rc=static_cast<R*>(p)->stage(batch);
  RiscExperimental::NativeReplicaCore::wipe(changes,sizeof changes);
  switch(rc){case R::Core::Ok:return S::CommitOk;case R::Core::Busy:return S::CommitBusy;case R::Core::Stale:return S::CommitStale;case R::Core::NoSpace:case R::Core::NoMemory:return S::CommitCapacity;case R::Core::Invalid:case R::Core::Conflict:return S::CommitConflict;default:return S::CommitIo;}
 }});
 return int(result);
}
