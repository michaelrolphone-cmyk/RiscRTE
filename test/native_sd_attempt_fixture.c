/* Exact external X4 driver/shared FatFs through its source-bound sector fixture.
 * Only native task/Sync/work callbacks are replaced with the private test seam. */
#include "native_sd_attempt_fixture.h"
static const risc_storage_worker_v1 fixture_worker;
#define X4PRO_SD_ASYNC 1
#define main unused_legacy_fixture_main
#include X4_SD_TEST_SOURCE
#undef main
static bool native_scope,native_opened,open_failure;
static unsigned native_reads,native_writes;
static bool on_worker(void*c){(void)c;return fixture_provider_worker();}
static uint64_t work_now(void*c){(void)c;assert(fixture_raw_io_allowed());return now_ms;}
static void work_delay(void*c,uint32_t ms){(void)c;assert(native_scope&&fixture_raw_io_allowed());now_ms+=ms;}
static bool work_power(void*c,bool level){(void)c;(void)level;return native_scope&&fixture_raw_io_allowed();}
static bool work_open(void*c,risc_sdmmc_card_info_v1*out){(void)c;if(!native_scope||!fixture_raw_io_allowed()||native_opened)return false;if(open_failure)return false;native_opened=true;*out=(risc_sdmmc_card_info_v1){sizeof(*out),512,card_sectors,20000000,0};return true;}
static bool work_read(void*c,uint64_t lba,uint32_t n,void*out){(void)c;if(!native_scope||!native_opened||!fixture_raw_io_allowed())return false;assert(n&&n<=8&&lba+n<=card_sectors);fixture_before_read();++native_reads;if(absent)return false;memcpy(out,card_image+lba*512,n*512);return true;}
static bool work_write(void*c,uint64_t lba,uint32_t n,const void*in){(void)c;if(!native_scope||!native_opened||!fixture_raw_io_allowed())return false;assert(n&&n<=8&&lba+n<=card_sectors);++native_writes;if(absent)return false;memcpy(card_image+lba*512,in,n*512);return true;}
static bool work_sync(void*c){(void)c;return native_scope&&native_opened&&fixture_raw_io_allowed();}
static bool work_close(void*c){(void)c;if(!native_scope||!fixture_raw_io_allowed())return false;native_opened=false;return true;}
static const risc_storage_work_io_v1 work_io={NULL,on_worker,work_now,work_power,work_delay,work_open,work_read,work_write,work_sync,work_close};
static int32_t work_bind(void*c,risc_storage_work_execute_v1 fn,void*ctx){(void)c;assert(native_scope);return fixture_worker_bind(fn,ctx);}
static int32_t work_submit(void*c,const void*p,uint32_t n,uint64_t*out){(void)c;return fixture_worker_submit(p,n,out);}
static int32_t work_collect(void*c,uint64_t t,void*p,uint32_t n,uint32_t*size,int32_t*status){(void)c;return fixture_worker_collect(t,p,n,size,status);}
static int32_t work_cancel(void*c,uint64_t t){(void)c;return fixture_worker_cancel(t);}
static int32_t work_unbind(void*c){(void)c;return fixture_worker_unbind();}
static int32_t work_claim(void*c,const risc_storage_work_scope_v1*q){(void)c;assert(!native_scope&&q->power==5&&q->clk==41&&q->cmd==42&&q->dat0==40);native_scope=true;return 0;}
static int32_t work_release(void*c){(void)c;assert(!native_opened);native_scope=false;return 0;}
static const risc_storage_worker_v1 fixture_worker={1,sizeof(fixture_worker),NULL,work_bind,work_submit,work_collect,work_cancel,work_unbind,work_claim,work_release,&work_io};
static bool private_sync_owner(void*c){(void)c;return fixture_sync_admission();}
static const risc_provider_sync_api_v1 private_sync={1,sizeof(private_sync),NULL,private_sync_owner,sync_create,sync_take,sync_unlock,sync_destroy};
const risc_storage_volume_api_v1*fixture_start(void){format(false);deps[3].api=&private_sync;assert(t5_driver_get(2)&&START());assert(!mounted&&!native_opened&&!native_reads);return &logging_api.prepared.base.sleep.terminal.power.volume.base;}
void fixture_stop(void){assert(quiesce());stop();assert(!native_scope);free(card_image);card_image=NULL;}
int fixture_observe(void){return observe_state(NULL);}
void fixture_retain(void){gpio_retained=true;}
void fixture_absent(bool value){absent=value;}
void fixture_open_failure(bool value){open_failure=value;}
void fixture_unavailable(void){mounted=false;}
unsigned fixture_reads(void){return native_reads;}
unsigned fixture_writes(void){return native_writes;}
void fixture_rejected_calls(void){rejected_calls();}
bool fixture_scope_open(void){return native_opened;}

uint64_t fixture_foreign_begin(void){uint64_t s=0;assert(async_begin(NULL,&s)==0);return s;}
bool fixture_foreign_end(uint64_t s){return async_end(NULL,s)==0;}
