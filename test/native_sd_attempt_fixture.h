#pragma once
#include <RiscStorageWorkerV1.h>
#include <RiscStoragePackageAsyncV1.h>
#ifdef __cplusplus
extern "C" {
#endif
const risc_storage_volume_api_v1*fixture_start(void);
void fixture_stop(void);
int fixture_observe(void);
void fixture_retain(void);
void fixture_absent(bool);
void fixture_open_failure(bool);
void fixture_unavailable(void);
unsigned fixture_reads(void);
unsigned fixture_writes(void);
void fixture_rejected_calls(void);
bool fixture_scope_open(void);
uint64_t fixture_foreign_begin(void);
bool fixture_foreign_end(uint64_t);
int fixture_worker_bind(risc_storage_work_execute_v1,void*);
int fixture_worker_submit(const void*,uint32_t,uint64_t*);
int fixture_worker_collect(uint64_t,void*,uint32_t,uint32_t*,int32_t*);
int fixture_worker_cancel(uint64_t);
int fixture_worker_unbind(void);
bool fixture_sync_admission(void);
bool fixture_provider_worker(void);
bool fixture_raw_io_allowed(void);
void fixture_before_read(void);
#ifdef __cplusplus
}
#endif
