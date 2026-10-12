#pragma once
#include "native_sd_attempt_fixture.h"
#ifdef __cplusplus
extern "C" {
#endif
void consumers_seed_begin(void);
void consumers_seed_file(const char*,const void*,unsigned);
void consumers_seed_save(const char*);
void consumers_seed_rename(const char*,const char*);
void consumers_seed_load(const char*);
void consumers_trace_enable(void);
unsigned consumers_observations(void);
unsigned consumers_refreshes(void);
unsigned consumers_begins(void);
unsigned consumers_recoveries(void);
unsigned consumers_submissions(void);
void consumers_fail_next_result(void);
void consumers_malformed_busy(void);
void consumers_fail_page_read(void);
unsigned consumers_page_read_faults(void);
unsigned consumers_mutations(void);
bool consumers_observe_allowed(void);
#ifdef __cplusplus
}
#endif
