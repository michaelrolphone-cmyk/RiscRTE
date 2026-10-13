#include "runtime/packages/PackageIdentity.h"
#include "runtime/update/PairedBank.h"
#include "runtime/update/Cohort.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
using namespace RiscUpdate;
static unsigned mutations=0;
static Backend backend(){return {nullptr,
 [](void*){return 0u;},
 [](void*,unsigned,unsigned,uint32_t,void*,uint32_t){return true;},
 [](void*,unsigned,unsigned,uint32_t){++mutations;return true;},
 [](void*,unsigned,unsigned,uint32_t,const void*,uint32_t){++mutations;return true;},
 [](void*,unsigned){++mutations;return true;},
 [](void*,unsigned,const Record&){++mutations;return true;},
 [](void*){return true;},[](void*,const void*,uint32_t){return true;},[](void*,uint8_t*){return true;},
 [](void*,unsigned){++mutations;return true;},[](void*,const void*,uint32_t){++mutations;return true;},
 [](void*,unsigned){return true;},[](void*){return true;},
 [](void*,unsigned,uint32_t){return true;},[](void*,unsigned){++mutations;return true;},
 [](void*,unsigned){return true;}};}
int main(int argc,char**argv){
 assert(argc==2);
 constexpr uint32_t sizes[]={0,0x4f0000,0x510000,0x550000};
 const char* layouts[]={"", "riscrte-paired-16m-v1","riscrte-paired-appdata-v2","riscrte-paired-appdata-v3"};
 assert(StoreAbi>=1 && StoreAbi<=3 && StoreBytes==sizes[StoreAbi] && !strcmp(Layout,layouts[StoreAbi]));
 assert(FirmwareOffset[1]==(StoreAbi==3?0x840000:0x800000));
 assert(StoreOffset[1]==(StoreAbi==3?0xaa0000:StoreAbi==2?0xae0000:0xb00000));
 if(StoreAbi>=2)assert(FirmwareOffset[0]+FirmwareBytes==0x270000 && StoreOffset[0]==0x2f0000);
 uint8_t digest[32]{};
 for(unsigned bank=0;bank<2;++bank){
  auto current=makeRecord(bank,64,digest,digest);assert(validRecord(current,bank));
  for(unsigned abi=1;abi<=3;++abi){
   auto foreign=current;foreign.storeAbi=abi;foreign.storeSize=sizes[abi];foreign.crc=crc32(&foreign,offsetof(Record,crc));
   assert(validRecord(foreign,bank)==(abi==StoreAbi));
   if(abi!=StoreAbi){Transaction rejected(backend());assert(!rejected.initialize(bank,foreign));}
   for(bool app:{false,true}){
    Transaction transaction(backend());assert(transaction.initialize(bank,current));
    risc_bank_image_v1 image{};image.struct_size=sizeof(image);image.size=64;image.store_abi=abi;
    uint64_t token=99;const unsigned before=mutations;
    if(abi!=StoreAbi){assert(transaction.begin(app,image,&token)==RISC_BANK_INVALID && !token && mutations==before);}
   }
   Transaction transaction(backend());assert(transaction.initialize(bank,current));
   risc_bank_cohort_v1 cohort{};cohort.struct_size=sizeof(cohort);cohort.store_abi=abi;cohort.firmware_size=64;cohort.store_size=sizes[abi];
   uint64_t token=99;const unsigned before=mutations;
   if(abi!=StoreAbi){assert(transaction.beginCohort(cohort,&token)==RISC_BANK_INVALID && !token && mutations==before);}
  }
  for(uint32_t size:{StoreBytes-1,StoreBytes+1,0xffffffffu}){
   Transaction transaction(backend());assert(transaction.initialize(bank,current));
   risc_bank_cohort_v1 cohort{};cohort.struct_size=sizeof(cohort);cohort.store_abi=StoreAbi;cohort.firmware_size=64;cohort.store_size=size;
   uint64_t token=99;const unsigned before=mutations;
   assert(transaction.beginCohort(cohort,&token)==RISC_BANK_INVALID && !token && mutations==before);
  }
 }
 // The native cohort parser rejects crossed layout strings independently of
 // ABI. Test every ABI/layout pairing against each separately compiled target.
 for(unsigned abi=1;abi<=3;++abi)for(unsigned layout=1;layout<=3;++layout){
  std::string file=std::string(argv[1])+"/cohort.json";FILE* f=fopen(file.c_str(),"wb");assert(f);
  fprintf(f,"{\"schema\":\"riscrte.cohort\",\"schema_version\":1,\"product\":\"twatch-s3\",\"version\":\"1.2.3\",\"runtime_version\":\"0.2.67\",\"source_repo\":\"example/watch\",\"source_revision\":\"1111111111111111111111111111111111111111\",\"layout\":\"%s\",\"store_abi\":%u,\"firmware_size\":64,\"firmware_sha256\":\"1111111111111111111111111111111111111111111111111111111111111111\"}",layouts[layout],abi);
  assert(!fclose(f));CohortIdentity identity{};assert(readCohort(argv[1],identity)==(abi==StoreAbi && layout==StoreAbi));
 }
 puts("PASS: native layout, paired journal, pre-mutation OTA rejection and cohort identity matrix");
}
