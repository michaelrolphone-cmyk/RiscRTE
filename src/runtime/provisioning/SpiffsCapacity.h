#pragma once
#include "Profile.h"
#include <cstdint>
namespace RiscProvision { namespace SpiffsCapacity {
// Pinned ESP-IDF 4.4.7 SPIFFS geometry: 256-byte pages, 4096-byte blocks,
// 16-bit object/page indexes, 32-byte names and 4-byte mtime metadata. Payload
// input remains <=4096 bytes; StoreFiles coalesces unbuffered 8192-byte writes.
constexpr uint32_t PageBytes=256,BlockBytes=4096,WriteBytes=8192;
constexpr uint32_t DataBytes=251,FirstIndexes=103,NextIndexes=124;
constexpr uint32_t PagesPerBlock=15,ReserveBlocks=4;
constexpr uint64_t filePages(uint32_t bytes){
 const uint64_t data=(uint64_t(bytes)+DataBytes-1)/DataBytes;
 const uint64_t indexes=1+(data>FirstIndexes?(data-FirstIndexes+NextIndexes-1)/NextIndexes:0);
 const uint64_t writes=(uint64_t(bytes)+WriteBytes-1)/WriteBytes;
 // Charge live data/index pages and every possible replacement index/header
 // caused by append+mtime. Do not assume garbage has already been reclaimed.
 return bytes?data+2*indexes+writes-1:UINT64_MAX;
}
inline bool fits(const Profile& profile,uint32_t partitionBytes){
 if(profile.count<3 || profile.count>MaxFiles || partitionBytes%BlockBytes || partitionBytes/BlockBytes<=ReserveBlocks)return false;
 const uint64_t budget=(partitionBytes/BlockBytes-ReserveBlocks)*PagesPerBlock;
 uint64_t used=filePages(32); // committed profile identity
 for(size_t i=0;i<profile.count;++i){
  const auto bytes=profile.files[i].bytes;if(!bytes || bytes>MaxFileBytes)return false;
  used+=filePages(bytes);if(used>budget)return false;
 }
 return used<=budget;
}
} }
