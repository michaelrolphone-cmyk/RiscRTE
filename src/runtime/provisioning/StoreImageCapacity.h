#pragma once
#include "SpiffsCapacity.h"
#include <cstdint>

namespace RiscProvision { namespace StoreImageCapacity {
// Pinned ESP-IDF 4.4.7 SPIFFS: little-endian 16-bit object/page indexes,
// 4096-byte blocks, 256-byte pages, USE_MAGIC=1 and USE_MAGIC_LENGTH=1.
// Source: pellepl/spiffs 0dbb3f71c5f6fae3747a9d935372773762baf852,
// src/spiffs_nucleus.h:141-142,165-166,192-199,232-237;
// src/spiffs_nucleus.c:330-338 and src/spiffs_gc.c:280-283.
// Inspect the persisted inactive image before mounting it: SPIFFS mount can
// repair one bad-magic block by erasing it. No repair is accepted here.
// This checks physical capacity and local page-header safety, not complete
// page-reference relationships, file hashes, exact inventory, or graph admission.
// Those checks remain mandatory.
constexpr uint32_t BlockBytes=SpiffsCapacity::BlockBytes;
constexpr uint32_t PageBytes=SpiffsCapacity::PageBytes;
constexpr uint32_t PagesPerBlock=SpiffsCapacity::PagesPerBlock;
constexpr uint32_t ReserveBlocks=SpiffsCapacity::ReserveBlocks;
static_assert(BlockBytes==4096 && PageBytes==256 && PagesPerBlock==15 && ReserveBlocks==4,
              "Revalidate compact-image geometry if streamed SPIFFS changes");

// Return the exact number of bytes read. Errors and short reads fail closed.
// The caller supplies storage for at least one block and serializes the reader
// with all writes. The verifier issues only aligned, in-range 4096-byte reads.
using Read=uint32_t (*)(void*,uint32_t,void*,uint32_t);
struct Counts {
 uint32_t occupiedPages=0; // Includes deleted pages: they still need an erase.
 uint32_t deletedPages=0;
 uint32_t freePages=0;
 uint32_t freeBlocks=0;
};

inline uint16_t little16(const uint8_t* bytes){
 return uint16_t(uint16_t(bytes[0]) | (uint16_t(bytes[1])<<8));
}
inline bool erased(const uint8_t* bytes,uint32_t size){
 for(uint32_t i=0;i<size;++i)if(bytes[i]!=0xff)return false;
 return true;
}
inline uint32_t little32(const uint8_t* bytes){
 return uint32_t(little16(bytes)) | (uint32_t(little16(bytes+2))<<16);
}
inline bool livePageSafe(const uint8_t* page,uint16_t id,uint32_t partitionBytes){
 const uint8_t flags=page[4];const bool index=(id&0x8000u)!=0;
 // Match the lookup identity; require used, finalized, undeleted pages and
 // the index/data kind encoded by SPIFFS_PH_FLAG_INDEX (active low).
 // Pinned spiffs_nucleus.h:334-349 validates these flags during file reads.
 if(!(id&0x7fffu) || little16(page)!=id || (flags&3u) || !(flags&0x80u) ||
    index==bool(flags&4u))return false;
 if(index && little16(page+2)==0){
  // The pinned directory visitor copies this field with strcpy before our
  // inventory checks run (spiffs_hydrogen.c:1068-1084). A NUL outside the
  // 32-byte name, including in mtime metadata, is not sufficient.
  bool terminated=false;
  for(uint32_t n=0;n<32;++n)if(page[13+n]==0){terminated=true;break;}
  const uint32_t size=little32(page+8);
  // Provisioning accepts only nonempty regular files. Reject deleted index
  // headers, unsupported types and impossible/undefined file sizes up front.
  if(!terminated || !(flags&0x40u) || page[12]!=1 || !size ||
     size>MaxFileBytes || size>partitionBytes)return false;
 }
 return true;
}

inline bool fits(uint32_t partitionBytes,void* context,Read read,
                 void* sectorBuffer,uint32_t sectorCapacity,Counts* counts=nullptr){
 if(counts)*counts=Counts{};
 // Block alignment also makes the 16-bit page-index bound stricter than the
 // IDF object-id bound. In particular, a full 16 MiB image is unsupported.
 if(!read || !sectorBuffer || sectorCapacity<BlockBytes ||
    partitionBytes%BlockBytes || partitionBytes/BlockBytes<=ReserveBlocks ||
    partitionBytes/PageBytes>UINT16_MAX)return false;
 const uint32_t blocks=partitionBytes/BlockBytes;
 const uint32_t budget=(blocks-ReserveBlocks)*PagesPerBlock;
 auto* sector=static_cast<uint8_t*>(sectorBuffer);
 Counts result{};
 for(uint32_t block=0;block<blocks;++block){
  if(read(context,block*BlockBytes,sector,BlockBytes)!=BlockBytes)return false;
  // spiffs_nucleus.h SPIFFS_MAGIC and SPIFFS_MAGIC_PADDR: second-last
  // uint16 of the single lookup page. Its final uint16 is the erase count,
  // which may be 0xffff in a freshly generated image and is not a page slot.
  const uint16_t magic=uint16_t(0x20140529u ^ PageBytes ^ (blocks-block));
  if(little16(sector+PageBytes-4)!=magic)return false;
  // Only fifteen lookup entries exist. The unused lookup bytes must remain
  // erased in this geometry; neither magic nor erase count is allocatable.
  if(!erased(sector+PagesPerBlock*2,PageBytes-4-PagesPerBlock*2))return false;
  bool freeSeen=false,wholeBlockFree=true;
  for(uint32_t entry=0;entry<PagesPerBlock;++entry){
   const uint16_t id=little16(sector+entry*2);
   if(id==UINT16_MAX){
    freeSeen=true;
    // A free lookup entry alone cannot prove writable flash: an interrupted
    // page move can program the destination before occupying its lookup slot.
    if(!erased(sector+(entry+1)*PageBytes,PageBytes))return false;
    ++result.freePages;
   }else{
    // Pinned allocator/GC scans assume each block's free entries form a tail.
    if(freeSeen)return false;
    wholeBlockFree=false;
    ++result.occupiedPages;
    if(id==0)++result.deletedPages;
    else if(!livePageSafe(sector+(entry+1)*PageBytes,id,partitionBytes))return false;
    if(result.occupiedPages>budget)return false;
   }
  }
  if(wholeBlockFree)++result.freeBlocks;
 }
 if(result.freeBlocks<ReserveBlocks)return false;
 if(counts)*counts=result;
 return true;
}
} }
