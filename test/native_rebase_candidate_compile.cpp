#include <experimental/native_replica/NativeRebaseCandidate.h>
using R=RiscExperimental::NativeRebaseCandidate;
extern "C" int experimentalNativeRebaseCompile(R*receiver,const R::Remote*remote,const R::Batch*batch,uint64_t view,uint64_t frontier,void*out,uint32_t capacity){
 uint64_t id=0;uint32_t size=0;
 int rc=receiver->begin(*remote);rc+=receiver->get(view,"opaque",out,capacity,&size);
 rc+=receiver->pending(view,0,&id,out,capacity,&size);rc+=receiver->stage(*batch);
 rc+=receiver->finish(view,frontier);rc+=receiver->abandon();return rc;
}
