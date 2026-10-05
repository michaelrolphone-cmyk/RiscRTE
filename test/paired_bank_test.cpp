#include "runtime/update/PairedBank.h"
#include <openssl/sha.h>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>
using namespace RiscUpdate;
struct Model {
  std::vector<uint8_t> bytes[2][2],hashBytes,app;
  Record records[2]{};
  unsigned selected=0,operations=0,cut=0,clock=1,validations=0;
  bool clean=true,admitted=true,firmwareValid=true,selectFailure=false;
  unsigned storeOpens=0,storeFinishes=0;
  Model(){for(unsigned b=0;b<2;++b){bytes[b][0].resize(FirmwareBytes,0xff);bytes[b][1].resize(StoreBytes,uint8_t(b?0xff:0x5a));}
    for(unsigned i=0;i<9000;++i)bytes[0][0][i]=uint8_t(i);
    uint8_t fw[32],store[32];SHA256(bytes[0][0].data(),9000,fw);SHA256(bytes[0][1].data(),StoreBytes,store);
    records[0]=makeRecord(0,9000,fw,store);
  }
  bool action(){return !cut || ++operations!=cut;}
  Backend backend(){return {this,
    [](void* p){return static_cast<Model*>(p)->clock;},
    [](void* p,unsigned b,unsigned r,uint32_t o,void* d,uint32_t n){auto& m=*static_cast<Model*>(p);if(!m.action())return false;assert(n<=4096);memcpy(d,m.bytes[b][r].data()+o,n);return true;},
    [](void* p,unsigned b,unsigned r,uint32_t o){auto& m=*static_cast<Model*>(p);if(!m.action())return false;assert(b==1 && !(o%4096));memset(m.bytes[b][r].data()+o,0xff,4096);return true;},
    [](void* p,unsigned b,unsigned r,uint32_t o,const void* d,uint32_t n){auto& m=*static_cast<Model*>(p);if(!m.action())return false;assert(b==1 && n<=4096);memcpy(m.bytes[b][r].data()+o,d,n);return true;},
    [](void* p,unsigned b){auto& m=*static_cast<Model*>(p);if(!m.action())return false;assert(b==1);m.records[b]={};return true;},
    [](void* p,unsigned b,const Record& r){auto& m=*static_cast<Model*>(p);if(!m.action())return false;assert(b==1);m.records[b]=r;return true;},
    [](void* p){static_cast<Model*>(p)->hashBytes.clear();return true;},
    [](void* p,const void* d,uint32_t n){auto& m=*static_cast<Model*>(p);auto b=static_cast<const uint8_t*>(d);m.hashBytes.insert(m.hashBytes.end(),b,b+n);return true;},
    [](void* p,uint8_t* digest){auto& m=*static_cast<Model*>(p);SHA256(m.hashBytes.data(),m.hashBytes.size(),digest);return true;},
    [](void* p,unsigned b){auto& m=*static_cast<Model*>(p);assert(b==1);m.app.clear();return m.action();},
    [](void* p,const void* d,uint32_t n){auto& m=*static_cast<Model*>(p);if(!m.action())return false;auto b=static_cast<const uint8_t*>(d);m.app.insert(m.app.end(),b,b+n);return true;},
    [](void* p,unsigned b){auto& m=*static_cast<Model*>(p);if(!m.action() || !m.admitted)return false;assert(b==1);memcpy(m.bytes[1][1].data()+8192,m.app.data(),m.app.size());return true;},
    [](void* p){return static_cast<Model*>(p)->clean;},
    [](void* p,unsigned b,uint32_t n){auto& m=*static_cast<Model*>(p);assert(b==1 && n>=32);++m.validations;return m.firmwareValid;},
    [](void* p,unsigned b){auto& m=*static_cast<Model*>(p);if(!m.action())return false;assert(b==1 && validRecord(m.records[1],1));m.selected=b;return !m.selectFailure;}};}
};
std::vector<uint8_t> payload(9127,0x71);
risc_bank_image_v1 image(Model& m){risc_bank_image_v1 im{};im.struct_size=sizeof(im);im.size=payload.size();im.store_abi=1;
  memcpy(im.active_store_sha256,m.records[0].storeSha,32);SHA256(payload.data(),payload.size(),im.sha256);return im;}
risc_bank_status_v1 state(Transaction& t){risc_bank_status_v1 s{};s.struct_size=sizeof(s);assert(t.status(&s));return s;}
int advance(Transaction& t,uint64_t token){for(unsigned n=0;n<4096;++n){auto s=state(t);
  if(s.state==RISC_BANK_RECEIVING || s.state==RISC_BANK_READY || s.state==RISC_BANK_FAILED)return s.error;
  int rc=t.step(token,&s);if(rc)return rc;}assert(false);return 0;}
int feed(Transaction& t,uint64_t token){for(size_t at=0;at<payload.size();){uint32_t n=std::min(size_t(997),payload.size()-at);
  int rc=t.write(token,payload.data()+at,n);if(rc)return rc;at+=n;}return t.finish(token);}
void intact(Model& m){uint8_t digest[32];SHA256(m.bytes[0][0].data(),9000,digest);assert(!memcmp(digest,m.records[0].firmwareSha,32));
  SHA256(m.bytes[0][1].data(),StoreBytes,digest);assert(!memcmp(digest,m.records[0].storeSha,32));assert(validRecord(m.records[0],0));}
int main(){
  for(bool app:{false,true}){
    Model m;Transaction t(m.backend());assert(t.initialize(0,m.records[0]));auto im=image(m);uint64_t token;
    assert(t.begin(app,im,&token)==0 && token);assert(!t.exitSafe());assert(t.activate(token)==RISC_BANK_STATE);
    assert(advance(t,token)==0 && state(t).state==RISC_BANK_RECEIVING);
    assert(t.write(token+1,payload.data(),1)==RISC_BANK_STATE);assert(t.finish(token)==RISC_BANK_STATE);
    assert(feed(t,token)==0);assert(advance(t,token)==0);assert(state(t).state==RISC_BANK_READY);
    assert(m.selected==0 && validRecord(m.records[1],1));assert(t.activate(token)==0 && m.selected==1 && t.exitSafe());
    assert(t.abort(token)==RISC_BANK_STATE);assert(t.activated(token));intact(m);
    // A reset before confirmation selects the old firmware and therefore old
    // matching store; no independent journal pointer can mix these two banks.
    assert(m.records[0].bank==0 && m.records[1].bank==1);
  }
  // Fail every I/O around the start, clone boundaries, record and select. Full
  // transfer tail is separately corrupted below. Keep the old pair immutable.
  for(unsigned cut:{1u,2u,3u,4u,5u,7u,12u,25u,3793u,3794u,3795u,3796u,5057u,5060u}){
    Model m;m.cut=cut;Transaction t(m.backend());assert(t.initialize(0,m.records[0]));uint64_t token=0;auto im=image(m);
    int rc=t.begin(false,im,&token);if(!rc)rc=advance(t,token);if(!rc)rc=feed(t,token);if(!rc)rc=advance(t,token);if(!rc)rc=t.activate(token);
    intact(m);if(rc){assert(m.selected==0);m.cut=0;if(state(t).state==RISC_BANK_ACTIVATION_UNKNOWN){assert(t.abort(token)==RISC_BANK_STATE);assert(validRecord(m.records[1],1));}else{assert(t.abort(token)==0);assert(!validRecord(m.records[1],1));}}
  }
  {Model m;Transaction t(m.backend());assert(t.initialize(0,m.records[0]));auto im=image(m);im.active_store_sha256[0]^=1;uint64_t token=9;assert(t.begin(false,im,&token)==RISC_BANK_INVALID && token==0);assert(m.selected==0);}
  {Model m;Transaction t(m.backend());assert(t.initialize(0,m.records[0]));auto im=image(m);im.sha256[0]^=1;uint64_t token;assert(t.begin(false,im,&token)==0);assert(advance(t,token)==0);assert(feed(t,token)==RISC_BANK_INTEGRITY);assert(t.activate(token)==RISC_BANK_STATE);assert(t.abort(token)==0);intact(m);}
  {Model m;Transaction t(m.backend());assert(t.initialize(0,m.records[0]));auto im=image(m);uint64_t token;assert(t.begin(true,im,&token)==0);assert(advance(t,token)==0);m.admitted=false;assert(feed(t,token)==RISC_BANK_INTEGRITY);assert(t.abort(token)==0);intact(m);}
  {Model m;Transaction t(m.backend());assert(t.initialize(0,m.records[0]));auto im=image(m);uint64_t token;assert(t.begin(false,im,&token)==0);assert(advance(t,token)==0);assert(feed(t,token)==0);m.bytes[1][0][42]^=1;assert(advance(t,token)==RISC_BANK_INTEGRITY);assert(!validRecord(m.records[1],1));intact(m);}
  {Model m;Transaction t(m.backend());assert(t.initialize(0,m.records[0]));auto im=image(m);uint64_t token;assert(t.begin(false,im,&token)==0);assert(advance(t,token)==0);assert(feed(t,token)==0);m.bytes[1][1][42]^=1;assert(advance(t,token)==RISC_BANK_INTEGRITY);intact(m);}
  {Model m;Transaction t(m.backend());assert(t.initialize(0,m.records[0]));auto im=image(m);uint64_t token;assert(t.begin(false,im,&token)==0);m.clock=300002;auto s=state(t);assert(t.step(token,&s)==RISC_BANK_TIMEOUT);m.clean=false;assert(t.abort(token)==RISC_BANK_RETAINED && !t.exitSafe());m.clean=true;assert(t.abort(token)==0 && t.exitSafe());uint64_t next;assert(t.begin(false,im,&next)==0 && next!=token);assert(t.abort(token)==RISC_BANK_STATE);assert(t.abort(next)==0);}
  {Model m;Record broken=m.records[0];broken.firmwareSize^=1;Transaction t(m.backend());assert(!t.initialize(0,broken));}
  {Model m;Transaction t(m.backend());assert(t.initialize(0,m.records[0]));auto im=image(m);uint64_t token;assert(t.begin(true,im,&token)==0);
   while(state(t).state!=RISC_BANK_VERIFY_CLONE){auto s=state(t);assert(t.step(token,&s)==0);}
   m.bytes[1][1][42]^=1;assert(advance(t,token)==RISC_BANK_INTEGRITY);assert(m.app.empty());intact(m);}
  {Model m;Transaction t(m.backend());assert(t.initialize(0,m.records[0]));auto im=image(m);uint64_t token;assert(t.begin(false,im,&token)==0);
   assert(advance(t,token)==0 && feed(t,token)==0 && advance(t,token)==0);assert(state(t).done<=state(t).total);
   m.selectFailure=true;assert(t.activate(token)==RISC_BANK_RETAINED && m.selected==1);
   assert(state(t).state==RISC_BANK_ACTIVATION_UNKNOWN && t.activated(token));assert(t.abort(token)==RISC_BANK_STATE);
   assert(t.activate(token)==RISC_BANK_STATE && !t.exitSafe());assert(validRecord(m.records[1],1));intact(m);}
  // Private boot-owned whole-store staging reuses paired clone/readback and
  // selection ordering, without extending the public provider table.
  auto storeBackend=[](Model& m){auto b=m.backend();
    b.openStore=[](void* p,unsigned bank){auto& x=*static_cast<Model*>(p);assert(bank==1);++x.storeOpens;return x.action();};
    b.finishStore=[](void* p,unsigned bank,uint8_t* digest){auto& x=*static_cast<Model*>(p);assert(bank==1);++x.storeFinishes;
      if(!x.action() || !x.admitted)return false;
      SHA256(x.bytes[bank][1].data(),StoreBytes,digest);return true;};return b;};
  auto stage=[](Transaction& t,uint64_t token){for(unsigned i=0;i<5000;++i){if(t.stagingStore(token))return;auto s=state(t);assert(t.step(token,&s)==0);}assert(false);};
  {Model m;Transaction t(m.backend());assert(t.initialize(0,m.records[0]));uint64_t token=9;
   assert(t.beginStore(m.records[0].storeSha,&token)==RISC_BANK_UNAVAILABLE && token==0);intact(m);}
  {Model m;Transaction t(storeBackend(m));assert(t.initialize(0,m.records[0]));uint64_t token=0;uint8_t stale[32]{};
   assert(t.beginStore(stale,&token)==RISC_BANK_INVALID && !token);assert(!m.storeOpens);intact(m);}
  {Model m;Transaction t(storeBackend(m));assert(t.initialize(0,m.records[0]));uint64_t token=0;
   assert(t.beginStore(m.records[0].storeSha,&token)==0);assert(!t.stagingStore(token));assert(t.finishStore(token)==RISC_BANK_STATE);
   stage(t,token);assert(m.storeOpens==1 && m.app.empty());assert(!t.stagingStore(token+1));
   assert(t.write(token,payload.data(),1)==RISC_BANK_STATE && t.finish(token)==RISC_BANK_STATE);
   assert(t.activate(token)==RISC_BANK_STATE);m.bytes[1][1][42]^=1;
   assert(t.finishStore(token)==0 && !t.stagingStore(token));assert(advance(t,token)==0);
   assert(state(t).state==RISC_BANK_READY && m.storeFinishes==1 && m.selected==0);
   assert(t.activate(token)==0 && m.selected==1);assert(t.abort(token)==RISC_BANK_STATE);intact(m);}
  // Clone must pass integrity before any provisioning writer can open it.
  {Model m;Transaction t(storeBackend(m));assert(t.initialize(0,m.records[0]));uint64_t token;
   assert(t.beginStore(m.records[0].storeSha,&token)==0);
   while(state(t).state!=RISC_BANK_VERIFY_CLONE){auto s=state(t);assert(t.step(token,&s)==0);}
   m.bytes[1][1][17]^=1;assert(advance(t,token)==RISC_BANK_INTEGRITY && !m.storeOpens);assert(t.abort(token)==0);intact(m);}
  for(unsigned mode=0;mode<5;++mode){Model m;Transaction t(storeBackend(m));assert(t.initialize(0,m.records[0]));uint64_t token;
   assert(t.beginStore(m.records[0].storeSha,&token)==0);stage(t,token);m.bytes[1][1][42]^=1;
   if(mode==0){m.admitted=false;assert(t.finishStore(token)==RISC_BANK_INTEGRITY);}
   if(mode==1){m.clean=false;assert(t.finishStore(token)==RISC_BANK_RETAINED);assert(t.abort(token)==RISC_BANK_RETAINED);m.clean=true;}
   if(mode==2){m.clock=300002;assert(!t.stagingStore(token));assert(t.finishStore(token)==RISC_BANK_TIMEOUT);}
   if(mode==3){assert(t.finishStore(token)==0);m.bytes[1][1][43]^=1;assert(advance(t,token)==RISC_BANK_INTEGRITY);}
   if(mode==4){assert(t.finishStore(token)==0);m.bytes[1][0][43]^=1;assert(advance(t,token)==RISC_BANK_INTEGRITY);}
   assert(m.selected==0 && !validRecord(m.records[1],1));assert(t.abort(token)==0);intact(m);
   m.admitted=true;uint64_t next;auto ordinary=image(m);assert(t.begin(true,ordinary,&next)==0);assert(advance(t,next)==0);assert(!t.stagingStore(next));assert(feed(t,next)==0 && advance(t,next)==0);assert(t.abort(next)==0);}
  {Model m;Transaction t(storeBackend(m));assert(t.initialize(0,m.records[0]));uint64_t token;
   assert(t.beginStore(m.records[0].storeSha,&token)==0);stage(t,token);assert(t.finishStore(token)==0 && advance(t,token)==0);
   m.selectFailure=true;assert(t.activate(token)==RISC_BANK_RETAINED && m.selected==1);
   assert(state(t).state==RISC_BANK_ACTIVATION_UNKNOWN);assert(t.abort(token)==RISC_BANK_STATE);assert(!t.stagingStore(token));intact(m);}
  std::cout<<"Paired bank cloning, readback, activation ordering, fault cuts, stale handles, cleanup retention and old-pair preservation PASS\n";
}
