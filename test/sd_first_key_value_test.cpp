#include "runtime/storage/SdFirstKeyValue.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
using Store=RiscStorage::SdFirstKeyValue;
using Bytes=std::vector<uint8_t>;
std::map<uint32_t,Bytes> journals;
int writeResult=RISC_KEY_VALUE_OK;bool uncertainPersist;unsigned writes,legacyReads;
const Store::Journal backend={nullptr,
 [](void*,uint32_t ns,void*out,uint32_t capacity,uint32_t*n)->int32_t{auto i=journals.find(ns);if(i==journals.end())return RISC_KEY_VALUE_NOT_FOUND;assert(capacity>=i->second.size());*n=i->second.size();memcpy(out,i->second.data(),*n);return RISC_KEY_VALUE_OK;},
 [](void*,uint32_t ns,const void*data,uint32_t n)->int32_t{++writes;if(writeResult==RISC_KEY_VALUE_OK||uncertainPersist)journals[ns]=Bytes(static_cast<const uint8_t*>(data),static_cast<const uint8_t*>(data)+n);return writeResult;},
 [](void*,uint32_t ns,const char*key,void*out,uint32_t,uint32_t*n)->int32_t{++legacyReads;assert(ns==3000);if(strcmp(key,"legacy"))return RISC_KEY_VALUE_NOT_FOUND;*n=3;memcpy(out,"old",3);return RISC_KEY_VALUE_OK;}};
Store::State state;Store::Workspace workspace;
Bytes snapshot(uint64_t&generation){Bytes out(Store::Bytes);uint32_t n=0;assert(Store::snapshot(state,3000,out.data(),out.size(),&n,&generation)==0);out.resize(n);return out;}
void read(const char*key,const char*expected){char out[64]{};uint32_t n=0;assert(Store::get(state,workspace,backend,3000,key,out,sizeof(out),&n)==0&&n==strlen(expected)&&!memcmp(out,expected,n));}
int main(){
 // A delivered native namespace is never copied into the 16-key SD cache.
 auto preserved=backend;preserved.legacyNamespace=[](void*,uint32_t,bool*exists)->int32_t{*exists=true;return 0;};
 for(unsigned restart=0;restart<3;++restart){state={};assert(Store::restore(state,workspace,preserved,3000,"new-app",nullptr,0,true)==0&&state.status==Store::LegacyNative&&journals.empty()&&!writes);}
 state={};
 char value[64];uint32_t n=0;uint64_t generation=0;
 assert(Store::get(state,workspace,backend,3000,"mode",value,64,&n)==RISC_KEY_VALUE_BUSY);
 assert(Store::put(state,workspace,backend,3000,"mode","x",1)==RISC_KEY_VALUE_BUSY);
 assert(Store::restore(state,workspace,backend,3000,"new-app",nullptr,0,true)==0&&state.status==Store::Dirty);
 assert(Store::put(state,workspace,backend,3000,"mode","one",3)==0&&state.generation==1);
 const auto firstJournal=journals[3000];read("mode","one");
 auto disk=snapshot(generation);assert(generation==1&&disk==firstJournal);
 // A write during an outstanding checkpoint is never cleared by its old ack.
 assert(Store::put(state,workspace,backend,3000,"mode","two",3)==0);
 assert(Store::acknowledge(state,workspace,backend,3000,generation)==RISC_KEY_VALUE_BUSY);
 state={};assert(Store::restore(state,workspace,backend,3000,"new-app",disk.data(),disk.size(),false)==0);read("mode","two");
 disk=snapshot(generation);assert(Store::acknowledge(state,workspace,backend,3000,generation)==0&&state.status==Store::Clean);
 assert(journals[3000].size()==Store::Header); // values removed from native journal
 state={};assert(Store::restore(state,workspace,backend,3000,"new-app",disk.data(),disk.size(),false)==0);read("mode","two");
 read("legacy","old");assert(legacyReads==1&&state.status==Store::Dirty);
 disk=snapshot(generation);assert(Store::acknowledge(state,workspace,backend,3000,generation)==0);
 state={};assert(Store::restore(state,workspace,backend,3000,"new-app",disk.data(),disk.size(),false)==0);read("legacy","old");assert(legacyReads==1);
 // Removal, replaced media, owner mismatch and corruption cannot roll back a
 // clean anchor or overwrite the surviving original per-key native settings.
 state={};assert(Store::restore(state,workspace,backend,3000,"new-app",nullptr,0,true)==RISC_KEY_VALUE_IO);
 assert(Store::restore(state,workspace,backend,3000,"other-app",disk.data(),disk.size(),false)==RISC_KEY_VALUE_IO);
 auto corrupt=disk;corrupt.back()^=1;assert(Store::restore(state,workspace,backend,3000,"new-app",corrupt.data(),corrupt.size(),false)==RISC_KEY_VALUE_IO);
 assert(Store::restore(state,workspace,backend,3001,"new-app",disk.data(),disk.size(),false)==RISC_KEY_VALUE_IO);
 assert(Store::restore(state,workspace,backend,3000,"new-app",disk.data(),disk.size(),false)==0);
 const auto stable=state;const auto stableJournal=journals[3000];
 for(bool persist:{false,true}){
  state=stable;journals[3000]=stableJournal;writeResult=RISC_KEY_VALUE_IO;uncertainPersist=persist;
  assert(Store::put(state,workspace,backend,3000,"mode","next",4)==RISC_KEY_VALUE_IO&&state.status==Store::Uncertain);
  assert(Store::get(state,workspace,backend,3000,"mode",value,64,&n)==RISC_KEY_VALUE_BUSY);
  assert(Store::put(state,workspace,backend,3000,"mode","bad",3)==RISC_KEY_VALUE_BUSY);
  writeResult=0;uncertainPersist=false;state={};assert(Store::restore(state,workspace,backend,3000,"new-app",disk.data(),disk.size(),false)==0);read("mode",persist?"next":"two");
 }
 state=stable;journals[3000]=stableJournal;writeResult=RISC_KEY_VALUE_BUSY;
 assert(Store::put(state,workspace,backend,3000,"mode","busy",4)==RISC_KEY_VALUE_BUSY&&state.status==Store::Clean);read("mode","two");writeResult=0;
 assert(Store::put(state,workspace,backend,3000,"mode",value,65)==RISC_KEY_VALUE_INVALID);
 assert(Store::put(state,workspace,backend,3000,"bad/key","x",1)==RISC_KEY_VALUE_INVALID);
 for(unsigned i=state.count;i<Store::Keys;++i){char key[16];snprintf(key,sizeof(key),"key%u",i);assert(Store::put(state,workspace,backend,3000,key,"x",1)==0);}
 assert(Store::put(state,workspace,backend,3000,"overflow","x",1)==RISC_KEY_VALUE_IO);
 disk=snapshot(generation);assert(disk.size()==Store::Bytes);state={};assert(Store::restore(state,workspace,backend,3000,"new-app",disk.data(),disk.size(),false)==0);
 puts("SD-first settings: journal-before-success, checkpoint anchors, interrupted writes, stale acknowledgements, lazy legacy migration, owner/media/corruption checks and bounded quota PASS");
}
