#include <experimental/native_replica/CheckedNativeReplicaStore.h>
extern "C" int experimentalRetainedStoreCompile(RiscExperimental::CheckedNativeReplicaStore*store,void*out){
 auto backend=store->backend();uint64_t object=0,pin=0;
 int rc=backend.readRoot(backend.context,out,RiscExperimental::NativeReplicaCore::RootBytes);
 rc+=backend.reserve(backend.context,1024,&object);rc+=store->pin(object,&pin);rc+=store->unpin(pin);
 store->finishOperation();return rc+(store->exitSafe()?1:0);
}
