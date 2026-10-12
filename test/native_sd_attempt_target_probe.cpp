#include "experimental/native_replica/NativeSdCompletion.h"
using Attempt=RiscExperimental::NativeSdAttempt;
using Completion=RiscExperimental::NativeSdCompletion;
extern "C" {
unsigned char native_sd_attempt_size[sizeof(Attempt)];
unsigned char native_sd_completion_size[sizeof(Completion)];
unsigned char native_sd_evidence_size[sizeof(Completion::Evidence)];
}
void native_sd_attempt_probe(Attempt&a,const risc_storage_volume_api_v1*v,
 const risc_storage_async_request_v1&q,const risc_storage_package_request_v1&p,
 risc_storage_async_result_v1&r,Completion&n,const Completion::Evidence&e){
 (void)a.acquire(v);(void)a.refresh();(void)a.collect(&r);(void)a.close();
 (void)a.openChild();(void)a.submit(q);(void)a.package(p);a.cancel();(void)a.release();
 (void)a.syncAdmission(1);(void)a.rawSubmit(1,&q,sizeof q);a.submitted(1,0,1);
 (void)a.rawCollect(1,1);(void)a.rawCancel(1,1);(void)a.exitSafe();
 (void)n.issue(e);n.cancel();n.unexpectedLoss();(void)n.workerStep();
 Completion::Result result{};(void)n.collect(&result);(void)n.retainedNativeResult();(void)n.exitSafe();
}
