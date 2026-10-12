/* Real source-bound X4/FatFs fixture, with synthetic content seeded before the
 * tested fresh attempt. No READY table or adapter state is invented. Explicit
 * fault results are injected only after collection of actual provider work. */
#include "native_sd_attempt_fixture.c"
#include "native_sd_consumers_fixture.h"
static unsigned observations,refreshes,begins,recoveries,submissions,mutations,page_faults;
static bool fail_next_result,malformed_busy,fail_page_read,page_read_active;
static int32_t traced_observe(void*c){assert(consumers_observe_allowed());++observations;return observe_state(c);}
static int32_t traced_begin(void*c,uint64_t*s){++begins;return async_begin(c,s);}
static int32_t traced_submit(void*c,uint64_t s,const risc_storage_async_request_v1*q,uint64_t*t){++submissions;if(q->operation==RISC_STORAGE_ASYNC_REFRESH)++refreshes;if(q->operation==RISC_STORAGE_ASYNC_REMOVE_FILE)++mutations;const int32_t rc=async_submit(c,s,q,t);if(!rc&&fail_page_read&&q->operation==RISC_STORAGE_ASYNC_READ_SMALL_FILE&&strstr(q->path,"/nr-0000000000000008-"))page_read_active=true;return rc;}
static int32_t traced_package(void*c,uint64_t s,const risc_storage_package_request_v1*q,uint64_t*t){++submissions;if(q->operation==RISC_STORAGE_PACKAGE_RECOVER)++recoveries;if(q->operation==RISC_STORAGE_PACKAGE_WRITE_SMALL||q->operation==RISC_STORAGE_PACKAGE_REPLACE)++mutations;return package_submit(c,s,q,t);}
static int32_t traced_collect(void*c,uint64_t s,uint64_t t,risc_storage_async_result_v1*r){int32_t rc=async_collect(c,s,t,r);if(page_read_active&&(rc==RISC_STORAGE_ASYNC_OK||rc==RISC_STORAGE_ASYNC_NOT_FOUND)){page_read_active=fail_page_read=false;++page_faults;r->status=RISC_STORAGE_ASYNC_IO;return RISC_STORAGE_ASYNC_IO;}if(malformed_busy&&rc==RISC_STORAGE_ASYNC_OK){malformed_busy=false;r->status=RISC_STORAGE_ASYNC_OK;return RISC_STORAGE_ASYNC_BUSY;}if(fail_next_result&&rc==RISC_STORAGE_ASYNC_OK){fail_next_result=false;r->status=RISC_STORAGE_ASYNC_IO;return RISC_STORAGE_ASYNC_IO;}return rc;}
void consumers_trace_enable(void){logging_api.observe=traced_observe;asynchronous_api.begin=traced_begin;asynchronous_api.submit=traced_submit;asynchronous_api.collect=traced_collect;package_api.submit_package=traced_package;}
void consumers_seed_begin(void){assert(!async_adapter.session&&!mounted&&!native_opened);assert(refresh(NULL)&&mounted);assert(mkdir_path(NULL,"/RTECFG"));assert(mkdir_path(NULL,"/RTECFG/00000003"));}
void consumers_seed_file(const char*path,const void*bytes,unsigned n){assert(!async_adapter.session&&mounted);const uint32_t h=file_open_write(NULL,path);assert(h);assert((!n||file_write(NULL,h,bytes,n)==n)&&file_sync(NULL,h)&&file_close(NULL,h,true));}
void consumers_seed_save(const char*path){assert(!has_handles()&&!async_adapter.session);assert(quiesce());stop();assert(!native_scope&&!native_opened);FILE*f=fopen(path,"wb");assert(f);assert(fwrite(card_image,512,card_sectors,f)==card_sectors);assert(!fclose(f));}
void consumers_seed_load(const char*path){assert(!mounted&&!native_opened&&!async_adapter.session&&!native_reads&&!native_writes);FILE*f=fopen(path,"rb");assert(f);assert(fread(card_image,512,card_sectors,f)==card_sectors);assert(fgetc(f)==EOF&&!fclose(f));}

unsigned consumers_observations(void){return observations;}
unsigned consumers_refreshes(void){return refreshes;}
unsigned consumers_begins(void){return begins;}
unsigned consumers_recoveries(void){return recoveries;}
unsigned consumers_submissions(void){return submissions;}
void consumers_fail_next_result(void){fail_next_result=true;}

void consumers_malformed_busy(void){malformed_busy=true;}

void consumers_fail_page_read(void){fail_page_read=true;}
unsigned consumers_page_read_faults(void){return page_faults;}
unsigned consumers_mutations(void){return mutations;}

void consumers_seed_rename(const char*from,const char*to){assert(!async_adapter.session&&mounted);assert(rename_path(NULL,from,to));}
