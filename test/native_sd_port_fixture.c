/* The pinned actual X4 driver and FatFs sector fixture are loaded by Runtime's
 * real graph. Only the Hardware sector transport and explicit faults are fake.
 * start receives unmodified, real CpuPort Sync/GPIO/storage-worker tables. */
#include "native_sd_port_fixture.h"
static const risc_storage_worker_v1 fixture_worker;
#define X4PRO_SD_ASYNC 1
#define t5_driver_get actual_x4_driver_get
#define main unused_legacy_fixture_main
#include X4_SD_TEST_SOURCE
#undef main
#undef t5_driver_get
static unsigned fixture_refreshes,fixture_submits,fixture_checked,fixture_ends,fixture_replaces,fixture_package_submits,fixture_raw_queued,fixture_package_queued;
static bool fixture_close_fault,fixture_read_fault,fixture_sync_fault;
static unsigned fixture_checked_fault;
static bool card_opened,fixture_open_fault;
static bool fixture_card_open(risc_sdmmc_card_info_v1*out){assert(native_sd_port_hardware_allowed()&&!card_opened);if(fixture_open_fault)return false;card_opened=true;*out=(risc_sdmmc_card_info_v1){sizeof(*out),512,card_sectors,20000000,0};return true;}
static bool fixture_card_read(uint64_t lba,uint32_t n,void*out){assert(native_sd_port_hardware_allowed()&&card_opened&&n&&n<=8&&lba+n<=card_sectors);native_sd_port_before_read();++card_reads;if(fixture_read_fault)return false;memcpy(out,card_image+lba*512,n*512);return true;}
static bool fixture_card_write(uint64_t lba,uint32_t n,const void*in){assert(native_sd_port_hardware_allowed()&&card_opened&&n&&n<=8&&lba+n<=card_sectors);++card_writes;memcpy(card_image+lba*512,in,n*512);return true;}
static bool fixture_card_sync(void){assert(native_sd_port_hardware_allowed()&&card_opened);return !fixture_sync_fault;}
static bool fixture_card_close(void){assert(native_sd_port_hardware_allowed());if(fixture_close_fault)return false;card_opened=false;return true;}
static void fixture_fault(unsigned which){if(which==8)fixture_open_fault=true;if(which==1)fixture_read_fault=true;if(which==2)fixture_close_fault=true;if(which==3)gpio_retained=true;if(which==4)async_adapter.retained=true;if(which==5)fixture_sync_fault=true;if(which==6||which==7)fixture_checked_fault=which;}
static unsigned fixture_counter(unsigned which){switch(which){case 1:return fixture_refreshes;case 2:return fixture_submits;case 3:return fixture_checked;case 4:return fixture_ends;case 5:return card_reads;case 6:return card_writes;case 7:return fixture_replaces;case 8:return fixture_package_submits;case 9:return fixture_raw_queued;case 10:return fixture_package_queued;default:return 0;}}
static uint64_t fixture_session(void){return async_adapter.session;}
static int32_t traced_submit(void*c,uint64_t s,const risc_storage_async_request_v1*q,uint64_t*t){++fixture_submits;if(q->operation==RISC_STORAGE_ASYNC_REFRESH)++fixture_refreshes;const int32_t rc=async_submit(c,s,q,t);if(rc==RISC_STORAGE_ASYNC_OK)++fixture_raw_queued;return rc;}
/* Explicit malformed-result injection only after actual checked begin.
 * The provider keeps its real minted session, so these faults must retain. */
static int32_t traced_checked(void*c,uint64_t*s){++fixture_checked;const int32_t rc=async_begin_checked(c,s);if(!rc&&fixture_checked_fault==6)return RISC_STORAGE_ASYNC_BUSY;if(!rc&&fixture_checked_fault==7)*s=0;return rc;}
static int32_t traced_end(void*c,uint64_t s){++fixture_ends;return async_end(c,s);}
static int32_t traced_package(void*c,uint64_t s,const risc_storage_package_request_v1*q,uint64_t*t){++fixture_package_submits;if(q->operation==RISC_STORAGE_PACKAGE_REPLACE)++fixture_replaces;const int32_t rc=package_submit(c,s,q,t);if(rc==RISC_STORAGE_ASYNC_OK)++fixture_package_queued;return rc;}
static native_sd_port_fixture probe;
static bool fixture_start(const risc_provider_dependency_v1*d,size_t n){
 format(false);assert(start(d,n));
 probe=(native_sd_port_fixture){&logging_api.prepared.base.sleep.terminal.power.volume.base,sync_api,storage_worker,gpio_api,fixture_card_open,fixture_card_read,fixture_card_write,fixture_card_sync,fixture_card_close,fixture_fault,fixture_counter,fixture_session,has_handles,rejected_calls,quiesce};
 package_api.submit_package=traced_package;asynchronous_api.submit=traced_submit;asynchronous_api.end=traced_end;admission_api.begin_checked=traced_checked;
 native_sd_port_loaded(&probe);return true;
}
static risc_driver_service_v2 fixture_driver;
const risc_driver_v2*t5_driver_get(uint32_t abi){
 const risc_driver_v2*real=actual_x4_driver_get(abi);if(!real)return NULL;
 fixture_driver=driver;fixture_driver.poll.streams.driver.start=fixture_start;
 return &fixture_driver.poll.streams.driver;
}
__attribute__((destructor)) static void fixture_unloaded(void){free(card_image);card_image=NULL;native_sd_port_unloaded();}
