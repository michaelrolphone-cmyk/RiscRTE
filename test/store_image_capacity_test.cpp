#include "runtime/provisioning/StoreImageCapacity.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

namespace Capacity=RiscProvision::StoreImageCapacity;
using Bytes=std::vector<uint8_t>;
struct Source {
 Bytes bytes;
 uint32_t calls=0,failAt=UINT32_MAX,shortAt=UINT32_MAX;
 static uint32_t read(void* context,uint32_t offset,void* output,uint32_t size){
  auto& self=*static_cast<Source*>(context);
  assert(size==4096 && offset%4096==0 && offset==self.calls*4096);
  ++self.calls;
  assert(offset<=self.bytes.size() && size<=self.bytes.size()-offset);
  if(offset==self.failAt)return 0;
  const uint32_t actual=offset==self.shortAt?size-1:size;
  std::memcpy(output,self.bytes.data()+offset,actual);
  return actual;
 }
};
static void word(Bytes& bytes,size_t offset,uint16_t value){
 bytes.at(offset)=uint8_t(value);bytes.at(offset+1)=uint8_t(value>>8);
}
static Source empty(uint32_t blocks=8){
 Source source{Bytes(blocks*4096,0xff)};
 for(uint32_t block=0;block<blocks;++block)
  word(source.bytes,block*4096+252,uint16_t(0x20140529u^256u^(blocks-block)));
 return source;
}
static void occupy(Source& source,uint32_t block,uint32_t entry,uint16_t id=1){
 assert(id!=0xffff && entry<15);
 word(source.bytes,block*4096+entry*2,id);
 source.bytes.at(block*4096+(entry+1)*256)=0x69;
}
static bool check(Source& source,Capacity::Counts* counts=nullptr){
 uint8_t buffer[4097]; // An unaligned caller buffer must be safe.
 source.calls=0;
 return Capacity::fits(uint32_t(source.bytes.size()),&source,Source::read,
                       buffer+1,4096,counts);
}
static void rejected(Source source){
 Capacity::Counts counts{1,2,3,4};
 assert(!check(source,&counts));
 assert(counts.occupiedPages==0 && counts.deletedPages==0 &&
        counts.freePages==0 && counts.freeBlocks==0);
}
static void synthetic(){
 Capacity::Counts counts{};
 auto source=empty();assert(check(source,&counts));
 assert(source.calls==8 && counts.occupiedPages==0 && counts.deletedPages==0 &&
        counts.freePages==120 && counts.freeBlocks==8);
 for(uint32_t block=0;block<4;++block)for(uint32_t entry=0;entry<15;++entry)
  occupy(source,block,entry,entry%2?0:0x8001);
 assert(check(source,&counts)); // Exact occupied-page boundary, four free blocks.
 assert(counts.occupiedPages==60 && counts.deletedPages==28 &&
        counts.freePages==60 && counts.freeBlocks==4);
 auto over=source;occupy(over,4,0,0);rejected(over); // Deleted is not free.
 auto scattered=empty();for(uint32_t block=0;block<5;++block)occupy(scattered,block,0);
 rejected(scattered); // Plenty of free pages but only three whole free blocks.
 auto badMagic=source;badMagic.bytes[7*4096+252]^=1;rejected(badMagic);
 auto wrongLength=source;
 for(uint32_t block=0;block<8;++block)word(wrongLength.bytes,block*4096+252,0x0429);
 rejected(wrongLength); // Magic without the required partition-length binding.
 auto unformatted=empty();std::fill(unformatted.bytes.begin(),unformatted.bytes.end(),0xff);
 rejected(unformatted);
 auto padding=source;padding.bytes[4*4096+30]=0;rejected(padding);
 auto dirtyFree=source;dirtyFree.bytes[4*4096+256]=0;rejected(dirtyFree);
 auto dirtyTail=source;dirtyTail.bytes[7*4096+4095]=0;rejected(dirtyTail);
 auto hole=empty();occupy(hole,0,1);rejected(hole);
 auto deleted=source;for(uint32_t entry=0;entry<15;++entry)occupy(deleted,4,entry,0);
 rejected(deleted); // A fully deleted block is still occupied.
 auto readFailure=source;readFailure.failAt=7*4096;rejected(readFailure);
 auto shortRead=source;shortRead.shortAt=4096;rejected(shortRead);
 auto counters=source;for(uint32_t block=0;block<8;++block)
  word(counters.bytes,block*4096+254,uint16_t(block*123));
 assert(check(counters)); // Erase counters are metadata, not occupancy.
 uint8_t buffer[4096];
 for(uint32_t size:{0u,4096u,4u*4096,8u*4096+1,0x1000000u,UINT32_MAX}){
  source.calls=0;counts={1,2,3,4};
  assert(!Capacity::fits(size,&source,Source::read,buffer,sizeof(buffer),&counts));
  assert(source.calls==0 && counts.occupiedPages==0 && counts.deletedPages==0 &&
         counts.freePages==0 && counts.freeBlocks==0);
 }
 assert(!Capacity::fits(uint32_t(source.bytes.size()),&source,nullptr,buffer,sizeof(buffer)));
 assert(!Capacity::fits(uint32_t(source.bytes.size()),&source,Source::read,nullptr,sizeof(buffer)));
 assert(!Capacity::fits(uint32_t(source.bytes.size()),&source,Source::read,buffer,sizeof(buffer)-1));
 auto minimum=empty(5);assert(check(minimum));
 auto maximum=empty(4095);assert(check(maximum)); // Largest aligned 16-bit image.
 std::puts("PASS: compact-image geometry, exact budget, whole-block reserve, deleted pages, corruption and bounded read failures");
}
static uint32_t argument(const char* text){
 errno=0;char* end=nullptr;const unsigned long value=std::strtoul(text,&end,0);
 assert(!errno && end && end!=text && !*end && value<=UINT32_MAX);return uint32_t(value);
}
static void image(const char* filename,uint32_t offset,uint32_t length){
 std::ifstream file(filename,std::ios::binary);assert(file);
 Bytes bytes((std::istreambuf_iterator<char>(file)),{});
 assert(!file.bad() && bytes.size()<=UINT32_MAX && offset<=bytes.size());
 if(!length)length=uint32_t(bytes.size()-offset);
 assert(length<=bytes.size()-offset);
 Source source{Bytes(bytes.begin()+offset,bytes.begin()+offset+length)};
 Capacity::Counts counts{};assert(check(source,&counts));
 assert(source.calls==length/4096);
 std::printf("PASS: image=%s offset=0x%x bytes=%u occupied=%u deleted=%u free_pages=%u free_blocks=%u budget=%u\n",
  filename,offset,length,counts.occupiedPages,counts.deletedPages,counts.freePages,
  counts.freeBlocks,(length/4096-4)*15);
 // Every external fixture is also checked with a late malformed magic and a
 // late short read, after many successful sectors and populated local counts.
 auto malformed=source;malformed.bytes[length-4096+252]^=1;rejected(malformed);
 source.shortAt=length-4096;rejected(source);
}
int main(int argc,char** argv){
 if(argc==3 && !std::strcmp(argv[1],"--inspect")){
  std::ifstream file(argv[2],std::ios::binary);if(!file)return 2;
  Source source{Bytes((std::istreambuf_iterator<char>(file)),{})};
  if(file.bad() || source.bytes.size()>UINT32_MAX)return 2;
  Capacity::Counts counts{};if(!check(source,&counts))return 1;
  std::printf("{\"occupied_pages\":%u,\"deleted_pages\":%u,\"free_pages\":%u,\"free_blocks\":%u}\n",
   counts.occupiedPages,counts.deletedPages,counts.freePages,counts.freeBlocks);
  return 0;
 }
 assert(argc==1 || argc==2 || argc==4);synthetic();
 if(argc>1)image(argv[1],argc==4?argument(argv[2]):0,argc==4?argument(argv[3]):0);
}
