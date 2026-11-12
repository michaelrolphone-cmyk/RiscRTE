#include "runtime/storage/Lz4Block.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <iterator>
#include <vector>
#include <algorithm>
using Decoder=RiscStorage::Lz4Block;
struct Input {
 std::vector<uint8_t> packed;
 size_t offset=0,chunk=128,calls=0,failAt=0,lastProduced=0,maxGap=0;
 bool alive=true,lose=false,zero=false,over=false;
 Decoder::Status failure=Decoder::Io;
 static bool safe(void* p){return static_cast<Input*>(p)->alive;}
 Decoder::Status event(){
  assert(alive);++calls;
  if(calls==failAt){if(lose)alive=false;return failure;}
  return Decoder::Ok;
 }
 static Decoder::Status read(void* p,uint8_t* out,size_t wanted,size_t* got){
  Input& s=*static_cast<Input*>(p);assert(wanted&&wanted<=Decoder::InputBufferBytes);
  const auto rc=s.event();if(rc!=Decoder::Ok || !s.alive)return rc;
  if(s.zero){*got=0;return Decoder::Ok;}
  if(s.over){*got=wanted+1;return Decoder::Ok;}
  *got=std::min({wanted,s.chunk,s.packed.size()-s.offset});
  if(*got)memcpy(out,s.packed.data()+s.offset,*got);
  s.offset+=*got;return Decoder::Ok;
 }
 static Decoder::Status checkpoint(void* p,size_t produced){
  Input& s=*static_cast<Input*>(p);assert(produced>=s.lastProduced);
  s.maxGap=std::max(s.maxGap,produced-s.lastProduced);assert(s.maxGap<=Decoder::CheckpointBytes);
  s.lastProduced=produced;return s.event();
 }
 Decoder::Hooks hooks(){return {this,read,safe,checkpoint};}
};
static unsigned checks;
static Decoder::Status decode(Input& input,std::vector<uint8_t>& out,size_t expected,size_t capacity,size_t& written,size_t declared=0){
 auto rc=Decoder::decode(input.hooks(),declared?declared:input.packed.size(),out.data()+8,expected,capacity,&written);
 for(unsigned i=0;i<8;++i)assert(out[i]==0xa5&&out[out.size()-1-i]==0xa5);
 ++checks;return rc;
}
static void reject(std::vector<uint8_t> packed,size_t expected) {
 Input input;input.packed=packed;std::vector<uint8_t> out(expected+16,0xa5);size_t written;
 assert(decode(input,out,expected,expected,written)!=Decoder::Ok);
 assert(written<=expected);
}
static std::vector<uint8_t> file(const char* path){
 std::ifstream stream(path,std::ios::binary);assert(stream);
 return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
int main(int argc,char**argv){
 assert(argc>=1 && (argc-1)%2==0);
 for(int i=1;i<argc;i+=2){
  auto packed=file(argv[i]),raw=file(argv[i+1]);
  for(size_t chunk: {size_t(1),size_t(7),size_t(128)}){
   Input input;input.packed=packed;input.chunk=chunk;
   std::vector<uint8_t> out(raw.size()+16,0xa5);size_t written=0;
   assert(decode(input,out,raw.size(),raw.size(),written)==Decoder::Ok);
   assert(written==raw.size()&&(raw.empty()||!memcmp(out.data()+8,raw.data(),raw.size())));
   assert(input.offset==packed.size());
  }
 }
 // Full 16-bit match distance; no separate history allocation.
 {
  Input input;input.packed.push_back(0xfe);
  size_t extra=65535-15;
  while(extra>=255){input.packed.push_back(255);extra-=255;}
  input.packed.push_back(uint8_t(extra));
  std::vector<uint8_t> expected;
  for(size_t i=0;i<65535;++i){const uint8_t value=uint8_t(i*29);input.packed.push_back(value);expected.push_back(value);}
  input.packed.push_back(255);input.packed.push_back(255);
  for(size_t i=0;i<18;++i)expected.push_back(expected[i]);
  input.packed.push_back(0x50);
  for(uint8_t value:{uint8_t(1),uint8_t(2),uint8_t(3),uint8_t(4),uint8_t(5)}){input.packed.push_back(value);expected.push_back(value);}
  std::vector<uint8_t> output(expected.size()+16,0xa5);size_t written=0;
  assert(decode(input,output,expected.size(),expected.size(),written)==Decoder::Ok);
  assert(written==expected.size()&&!memcmp(output.data()+8,expected.data(),expected.size()));
 }
 // Five literal suffix bytes, with a distance-1 overlapping copy.
 const std::vector<uint8_t> packed={0x1f,'x',1,0,255,255,255,255,255,255,255,196,0x50,'1','2','3','4','5'};
 std::vector<uint8_t> raw(2006,'x');memcpy(raw.data()+2001,"12345",5);
 Input baseline;baseline.packed=packed;std::vector<uint8_t> out(raw.size()+16,0xa5);size_t written=0;
 assert(decode(baseline,out,raw.size(),raw.size(),written)==Decoder::Ok);
 assert(written==raw.size()&&!memcmp(out.data()+8,raw.data(),raw.size()));
 const size_t calls=baseline.calls;
 // Every read/checkpoint boundary: ordinary-OK lost custody and explicit errors.
 for(size_t at=1;at<=calls;++at)for(unsigned fault=0;fault<5;++fault){
  Input input;input.packed=packed;input.failAt=at;input.lose=fault==0;
  input.failure=fault==0?Decoder::Ok:fault==1?Decoder::Retained:fault==2?Decoder::Io:fault==3?Decoder::Invalid:static_cast<Decoder::Status>(73);
  std::fill(out.begin(),out.end(),0xa5);
  auto rc=decode(input,out,raw.size(),raw.size(),written);
  assert(rc==(fault==2?Decoder::Io:fault==3?Decoder::Invalid:Decoder::Retained));
  assert(input.calls==at);
 }
 Input lost;lost.packed=packed;lost.alive=false;
 assert(decode(lost,out,raw.size(),raw.size(),written)==Decoder::Retained&&!lost.calls);
 for(size_t cut=0;cut<packed.size();++cut)reject({packed.begin(),packed.begin()+cut},raw.size());
 auto trailing=packed;trailing.push_back(0);reject(trailing,raw.size());
 reject(packed,raw.size()-1);reject(packed,raw.size()+1);
 reject({0x10,'a',0,0,0},5); // zero distance
 reject({0x10,'a',2,0,0},5); // distance beyond produced bytes
 reject({0xf0,255,255,255},100); // length exceeds output / unterminated length
 reject({0x10,'a',1},10); // truncated distance
 reject({0x1f,'a',1,0,255},100); // malformed match length
 reject({0x10,'a',1,0},5); // missing final literal sequence
 reject({0x20,'a'},2); // literal stream truncated
 for(unsigned fault=0;fault<2;++fault){
  Input input;input.packed=packed;input.zero=fault==0;input.over=fault==1;
  assert(decode(input,out,raw.size(),raw.size(),written)==Decoder::Io);
 }
 Input small;small.packed=packed;
 assert(decode(small,out,raw.size(),raw.size()-1,written)==Decoder::Invalid&&!small.calls);
 Input empty;empty.packed={0};
 assert(decode(empty,out,0,0,written)==Decoder::Ok&&!written);
 Input huge;huge.packed={0xf0,255,255,255};
 assert(decode(huge,out,SIZE_MAX,SIZE_MAX,written)==Decoder::Invalid&&!written);
 Input nulls;nulls.packed={0};
 assert(Decoder::decode(nulls.hooks(),1,nullptr,1,1,&written)==Decoder::Invalid&&!nulls.calls);
 assert(Decoder::decode(nulls.hooks(),1,nullptr,0,0,nullptr)==Decoder::Invalid&&!nulls.calls);
 printf("LZ4 production decoder: %u checks; every callback fault boundary; zero heap; bounded output checkpoints PASS\n",checks);
}
