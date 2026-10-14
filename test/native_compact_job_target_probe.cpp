#include "experimental/native_replica/NativeReplicaJob.h"
#include "experimental/native_replica/CheckedFirstSdCheckpoint.h"
#include OLD_WORKER_HEADER
using namespace RiscExperimental;
static_assert(sizeof(RiscStorage::Worker)==sizeof(BaselineStorage::Worker),"default hardware mailbox layout");
static_assert(alignof(RiscStorage::Worker)==alignof(BaselineStorage::Worker),"default hardware mailbox alignment");
static_assert(sizeof(NativeReplicaControlJob)<256,"bounded numeric control");
static_assert(sizeof(NativeReplicaJob)<256,"credential payload is separately allocated");
extern "C" {
unsigned char compact_default_worker_size[sizeof(RiscStorage::Worker)];
unsigned char compact_control_worker_size[sizeof(RiscStorage::BasicWorker<8,16>)];
unsigned char compact_payload_job_size[sizeof(NativeReplicaJob)];
unsigned char compact_control_job_size[sizeof(NativeReplicaControlJob)];
unsigned char compact_first_checkpoint_size[sizeof(CheckedFirstSdCheckpoint)];
}
template<class Job> void compact_job_probe(Job&job,const void*payload,uint32_t size){
 uint64_t token=0;NativeReplicaJobResult result{};
 (void)job.begin(1,payload,size,&token);(void)job.workerStep();
 (void)job.collect(token,&result);(void)job.cancel(token);(void)job.exitSafe();
}
template void compact_job_probe(NativeReplicaJob&,const void*,uint32_t);
template void compact_job_probe(NativeReplicaControlJob&,const void*,uint32_t);
