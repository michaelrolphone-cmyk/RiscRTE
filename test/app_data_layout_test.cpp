#include "runtime/update/PairedBank.h"
#include <cassert>
#include <cstring>
using namespace RiscUpdate;
int main(){
 uint8_t digest[32]{};auto record=makeRecord(0,1154080,digest,digest);assert(validRecord(record,0));
#ifdef RISC_PAIRED_APP_DATA
 assert(FirmwareBytes==0x260000 && StoreAbi==2 && !strcmp(Layout,"riscrte-paired-appdata-v2"));
 assert(StoreOffset[0]==0x2f0000 && StoreOffset[1]==0xae0000 && StoreBytes==0x510000);
 assert(!strcmp(RISC_PAIRED_ABI_MARKER,"RISC_PAIRED_STORE_ABI:2"));record.storeAbi=1;
#else
 assert(FirmwareBytes==0x300000 && StoreAbi==1 && !strcmp(Layout,"riscrte-paired-16m-v1"));
 assert(StoreOffset[0]==0x310000 && StoreOffset[1]==0xb00000 && StoreBytes==0x4f0000);
 assert(!strcmp(RISC_PAIRED_ABI_MARKER,"RISC_PAIRED_STORE_ABI:1"));record.storeAbi=2;
#endif
 record.crc=crc32(&record,offsetof(Record,crc));assert(!validRecord(record,0));
 assert(JournalOffset==0xff2000);
 assert(FirmwareOffset[0]+FirmwareBytes<=StoreOffset[0]);
 assert(StoreOffset[0]+StoreBytes==FirmwareOffset[1]);
 assert(FirmwareOffset[1]+FirmwareBytes<=StoreOffset[1]);
 assert(StoreOffset[1]+StoreBytes==0xff0000);
}
