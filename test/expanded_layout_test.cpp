#include "runtime/packages/PackageIdentity.h"
#include "runtime/update/Cohort.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace RiscUpdate;
int main(){
 static_assert(sizeof("riscrte-paired-appdata-v3")<=sizeof(risc_bank_status_v1::layout),"layout bound");
 uint8_t sha[32]={1};Record current=makeRecord(0,256,sha,sha);assert(validRecord(current,0));
 for(unsigned abi=1;abi<=3;abi++){
  Record other=current;other.storeAbi=abi;other.storeSize=abi==1?0x4f0000:abi==2?0x510000:0x550000;other.crc=crc32(&other,offsetof(Record,crc));
  assert(validRecord(other,0)==(abi==StoreAbi));
  risc_bank_cohort_v1 c={};c.struct_size=sizeof(c);strcpy(c.product,"fixture");strcpy(c.version,"0.1.15");strcpy(c.runtime_version,"0.3.3");strcpy(c.source_repo,"owner/repo");memset(c.source_revision,'a',40);c.store_abi=abi;c.store_size=other.storeSize;c.firmware_size=256;
  assert(validCohortRequest(c)==(abi==StoreAbi));
 }
#ifdef RISC_PAIRED_APP_DATA_EXPANDED
 assert(FirmwareOffset[1]==0x840000&&StoreOffset[1]==0xaa0000&&StoreBytes==0x550000&&StoreAbi==3);
 assert(!strcmp(Layout,"riscrte-paired-appdata-v3"));
#else
 assert(FirmwareOffset[1]==0x800000);
#endif
 printf("Layout ABI %u: own record/cohort accepted; other ABI1/2/3 records/cohorts refused PASS\n",StoreAbi);
}
