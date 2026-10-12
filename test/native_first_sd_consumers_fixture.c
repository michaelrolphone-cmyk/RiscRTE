/* Each seed is created by the real provider in a separate process. */
#include "native_sd_consumers_fixture.c"
static unsigned first_collections,first_fault,first_faults,first_mutations_at_fault;
static int32_t first_collect(void*c,uint64_t s,uint64_t t,risc_storage_async_result_v1*r){
 int32_t rc=traced_collect(c,s,t,r);
 if(rc==RISC_STORAGE_ASYNC_OK&&++first_collections==first_fault){++first_faults;first_mutations_at_fault=mutations;r->status=RISC_STORAGE_ASYNC_IO;return RISC_STORAGE_ASYNC_IO;}
 return rc;
}
void first_trace_enable(unsigned fault){consumers_trace_enable();first_fault=fault;asynchronous_api.collect=first_collect;}
void first_seed_begin(unsigned parents){assert(!async_adapter.session&&!mounted&&!native_opened);assert(refresh(NULL)&&mounted);if(parents){assert(mkdir_path(NULL,"/RTECFG"));if(parents>1)assert(mkdir_path(NULL,"/RTECFG/00000003"));}}
unsigned first_collected(void){return first_collections;}
unsigned first_fault_count(void){return first_faults;}
unsigned first_fault_mutations(void){return first_mutations_at_fault;}
/* Explicit physical FAT fault fixture. Production capacity still runs every
 * real guarded checkpoint/sector read; no synthetic successful callback. */
static int capacity_limit_delta=1000;
static unsigned capacity_scans,capacity_scan_reads,capacity_scan_writes,capacity_scan_elapsed;
static bool capacity_budget;
static int32_t first_capacity_probe(void*c,const risc_storage_native_capacity_request_v1*q,risc_storage_native_capacity_result_v1*r){
 ++capacity_scans;
 if(capacity_limit_delta!=1000){
  assert(filesystem.fs_type==FS_FAT32&&!filesystem.wflag);
  unsigned left=risc_storage_native_capacity_required(q->map_bytes,q->pages,filesystem.csize*512u);
  left=capacity_limit_delta==-1000?0u:(unsigned)((int)left+capacity_limit_delta);
  for(DWORD entry=2;entry<filesystem.n_fatent;++entry){
   uint8_t*p=card_image+(size_t)filesystem.fatbase*512u+entry*4u;
   uint32_t old=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
   if(!(old&0x0fffffff)){if(left)--left;else for(unsigned fat=0;fat<filesystem.n_fats;++fat)put32(p+(size_t)fat*filesystem.fsize*512u,0x0fffffff);}
  }
  filesystem.free_clst=filesystem.n_fatent-2; /* Deliberately lying cached FSINFO. */
 }
 unsigned reads=native_reads,writes=native_writes;uint64_t began=now_ms;
 if(capacity_budget)bootlog_sector_limit=1;
 int32_t rc=fs_native_capacity(c,q,r);
 capacity_scan_elapsed+=(unsigned)(now_ms-began);
 capacity_scan_reads+=native_reads-reads;capacity_scan_writes+=native_writes-writes;
 return rc;
}
void first_capacity_budget(void){capacity_budget=true;}
unsigned first_capacity_elapsed(void){return capacity_scan_elapsed;}
void first_capacity_limit(int delta){capacity_limit_delta=delta;async_adapter.native_capacity=first_capacity_probe;}
void first_capacity_epoch_change(void){++native_mount_epoch;}
void first_capacity_receipt_change(void){++async_adapter.capacity_receipt.serial;}
void first_capacity_disable(void){capacity_api.capacity_version=2;}
unsigned first_capacity_scans(void){return capacity_scans;}
unsigned first_capacity_reads(void){return capacity_scan_reads;}
unsigned first_capacity_writes(void){return capacity_scan_writes;}
