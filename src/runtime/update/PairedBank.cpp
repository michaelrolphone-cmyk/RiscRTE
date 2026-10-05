#include "PairedBank.h"
#include <algorithm>
#include <cstring>
namespace RiscUpdate {
uint32_t crc32(const void* ptr,size_t n){
  auto p=static_cast<const uint8_t*>(ptr);uint32_t crc=~0u;
  while(n--){crc^=*p++;for(unsigned b=0;b<8;++b)crc=(crc>>1)^(0xedb88320u&uint32_t(-int32_t(crc&1)));}
  return ~crc;
}
bool validRecord(const Record& r,unsigned bank){
  return bank<2 && r.magic==RecordMagic && r.format==1 && r.bank==bank &&
    r.firmwareSize>=32 && r.firmwareSize<=FirmwareBytes && r.storeSize==StoreBytes &&
    r.storeAbi==RISC_BANK_STORE_ABI && !r.reserved && r.crc==crc32(&r,offsetof(Record,crc));
}
Record makeRecord(unsigned bank,uint32_t n,const uint8_t* fw,const uint8_t* store){
  Record r{};r.magic=RecordMagic;r.format=1;r.bank=bank;r.firmwareSize=n;r.storeSize=StoreBytes;
  r.storeAbi=RISC_BANK_STORE_ABI;memcpy(r.firmwareSha,fw,32);memcpy(r.storeSha,store,32);
  r.crc=crc32(&r,offsetof(Record,crc));return r;
}
bool Transaction::initialize(unsigned active,const Record& record){
  if(initialized_ || !validRecord(record,active) || !io_.now || !io_.read || !io_.erase || !io_.write ||
     !io_.invalidate || !io_.record || !io_.hashBegin || !io_.hashAdd || !io_.hashEnd ||
     !io_.openApp || !io_.writeApp || !io_.finishApp || !io_.cleanup || !io_.validateFirmware || !io_.select)return false;
  active_=active;target_=1-active;activeRecord_=record;initialized_=true;return true;
}
bool Transaction::timedOut()const{return uint32_t(io_.now(io_.context)-started_)>300000u;}
bool Transaction::hashStart(){offset_=0;return io_.hashBegin(io_.context);}
bool Transaction::status(risc_bank_status_v1* out)const{
  if(!out || out->struct_size<sizeof(*out) || !initialized_)return false;
  *out={};out->struct_size=sizeof(*out);out->state=state_;out->active_bank=active_;out->destination_bank=target_;
  out->firmware_capacity=FirmwareBytes;out->store_capacity=StoreBytes;out->error=error_;
  out->done=state_==RISC_BANK_RECEIVING?received_:offset_;
  out->total=(state_==RISC_BANK_COPY_STORE || state_==RISC_BANK_VERIFY_STORE || state_==RISC_BANK_VERIFY_CLONE)?StoreBytes:
    state_==RISC_BANK_RECEIVING?image_.size:targetRecord_.firmwareSize;
  if(state_==RISC_BANK_READY || state_==RISC_BANK_ACTIVATED || state_==RISC_BANK_ACTIVATION_UNKNOWN)out->done=out->total=1;
  if(state_==RISC_BANK_IDLE || state_==StoreStaging)out->done=out->total=0;
  if(out->done>out->total)out->total=out->done;
  memcpy(out->active_store_sha256,activeRecord_.storeSha,32);return true;
}
int32_t Transaction::begin(bool app,const risc_bank_image_v1& image,uint64_t* token){
  if(token)*token=0;
  if(!token || !initialized_ || state_!=RISC_BANK_IDLE || serial_==UINT64_MAX)return RISC_BANK_STATE;
  if(image.struct_size<sizeof(image) || image.store_abi!=RISC_BANK_STORE_ABI || !image.size ||
    image.size>(app?RISC_BANK_APP_MAX:FirmwareBytes) || (!app && image.size<32) ||
    memcmp(image.active_store_sha256,activeRecord_.storeSha,32))return RISC_BANK_INVALID;
  store_=false;app_=app;image_=image;token_=++serial_;*token=token_;started_=io_.now(io_.context);error_=0;received_=offset_=0;
  targetRecord_=activeRecord_;targetRecord_.bank=target_;if(!app)targetRecord_.firmwareSize=image.size;
  /* Invalidate only destination readiness BEFORE any target erase. */
  if(!io_.invalidate(io_.context,target_))return fail(RISC_BANK_IO);
  state_=app?RISC_BANK_COPY_FIRMWARE:RISC_BANK_COPY_STORE;return RISC_BANK_OK;
}
int32_t Transaction::beginStore(const uint8_t (&activeDigest)[32],uint64_t* token){
  if(token)*token=0;
  if(!io_.openStore || !io_.finishStore)return RISC_BANK_UNAVAILABLE;
  // Reuse the exact paired clone/invalidate/token/deadline path. No payload is
  // received through write(); private staging starts only after clone readback.
  risc_bank_image_v1 image{};image.struct_size=sizeof(image);image.size=1;
  image.store_abi=RISC_BANK_STORE_ABI;memcpy(image.active_store_sha256,activeDigest,32);
  const int32_t result=begin(true,image,token);
  if(result==RISC_BANK_OK)store_=true;
  return result;
}
int32_t Transaction::finishStore(uint64_t token){
  if(!live(token) || !store_ || state_!=StoreStaging)return RISC_BANK_STATE;
  if(timedOut())return fail(RISC_BANK_TIMEOUT);
  if(!io_.finishStore(io_.context,target_,stagedStoreDigest_))return fail(RISC_BANK_INTEGRITY);
  // A bounded backend may yield internally; recheck elapsed time before any
  // readiness write. Cleanup failure retains staging and denies activation.
  if(timedOut())return fail(RISC_BANK_TIMEOUT);
  if(!io_.cleanup(io_.context))return fail(RISC_BANK_RETAINED);
  if(!hashStart())return fail(RISC_BANK_IO);
  state_=RISC_BANK_VERIFY_FIRMWARE;return RISC_BANK_OK;
}
bool Transaction::writeRaw(unsigned region,uint32_t at,const void* ptr,uint32_t n){
  auto bytes=static_cast<const uint8_t*>(ptr);
  while(n){
    const uint32_t sector=at&~(SectorBytes-1u),part=std::min(n,SectorBytes-(at-sector));
    if(at==sector && !io_.erase(io_.context,target_,region,sector))return false;
    if(!io_.write(io_.context,target_,region,at,bytes,part))return false;
    at+=part;bytes+=part;n-=part;
  }
  return true;
}
int32_t Transaction::step(uint64_t token,risc_bank_status_v1* out){
  if(!live(token) || !out || out->struct_size<sizeof(*out))return RISC_BANK_STATE;
  if(state_==RISC_BANK_FAILED){status(out);return error_;}
  if(state_==RISC_BANK_ACTIVATION_UNKNOWN){status(out);return error_;}
  if(state_==StoreStaging && timedOut())return fail(RISC_BANK_TIMEOUT);
  if(state_==RISC_BANK_IDLE || state_==StoreStaging || state_==RISC_BANK_RECEIVING || state_==RISC_BANK_READY || state_==RISC_BANK_ACTIVATED){status(out);return RISC_BANK_OK;}
  if(timedOut())return fail(RISC_BANK_TIMEOUT);
  const bool copying=state_==RISC_BANK_COPY_FIRMWARE || state_==RISC_BANK_COPY_STORE;
  const unsigned region=(state_==RISC_BANK_COPY_STORE || state_==RISC_BANK_VERIFY_STORE || state_==RISC_BANK_VERIFY_CLONE)?1:0;
  const uint32_t total=region?StoreBytes:targetRecord_.firmwareSize;
  const uint32_t n=std::min(RISC_BANK_CHUNK_MAX,total-offset_);
  if(!io_.read(io_.context,copying?active_:target_,region,offset_,buffer_,n))return fail(RISC_BANK_IO);
  if(copying){if(!writeRaw(region,offset_,buffer_,n))return fail(RISC_BANK_IO);}
  else if(!io_.hashAdd(io_.context,buffer_,n))return fail(RISC_BANK_IO);
  offset_+=n;
  if(offset_==total){
    if(state_==RISC_BANK_COPY_FIRMWARE){state_=RISC_BANK_COPY_STORE;offset_=0;}
    else if(state_==RISC_BANK_COPY_STORE){
      if(!hashStart())return fail(RISC_BANK_IO);
      state_=app_?RISC_BANK_VERIFY_CLONE:RISC_BANK_RECEIVING;
    }else if(state_==RISC_BANK_VERIFY_CLONE){
      uint8_t digest[32];
      if(!io_.hashEnd(io_.context,digest))return fail(RISC_BANK_IO);
      if(memcmp(digest,activeRecord_.storeSha,32))return fail(RISC_BANK_INTEGRITY);
      if(store_){
        if(!io_.openStore(io_.context,target_))return fail(RISC_BANK_IO);
        state_=StoreStaging;
      }else{
        if(!io_.openApp(io_.context,target_) || !hashStart())return fail(RISC_BANK_IO);
        state_=RISC_BANK_RECEIVING;
      }
    }else if(state_==RISC_BANK_VERIFY_FIRMWARE){
      uint8_t digest[32];if(!io_.hashEnd(io_.context,digest))return fail(RISC_BANK_IO);
      if(memcmp(digest,app_?activeRecord_.firmwareSha:image_.sha256,32))return fail(RISC_BANK_INTEGRITY);
      memcpy(targetRecord_.firmwareSha,digest,32);
      if(!io_.validateFirmware(io_.context,target_,targetRecord_.firmwareSize))return fail(RISC_BANK_INTEGRITY);
      if(!hashStart())return fail(RISC_BANK_IO);
      state_=RISC_BANK_VERIFY_STORE;
    }else{
      if(!io_.hashEnd(io_.context,targetRecord_.storeSha))return fail(RISC_BANK_IO);
      if(!app_ && memcmp(targetRecord_.storeSha,activeRecord_.storeSha,32))return fail(RISC_BANK_INTEGRITY);
      if(store_ && memcmp(targetRecord_.storeSha,stagedStoreDigest_,32))return fail(RISC_BANK_INTEGRITY);
      targetRecord_.crc=crc32(&targetRecord_,offsetof(Record,crc));
      if(!io_.record(io_.context,target_,targetRecord_))return fail(RISC_BANK_IO);
      state_=RISC_BANK_READY;
    }
  }
  status(out);return RISC_BANK_OK;
}
int32_t Transaction::write(uint64_t token,const void* data,uint32_t n){
  if(!live(token) || state_!=RISC_BANK_RECEIVING)return RISC_BANK_STATE;
  if(!data || !n || n>RISC_BANK_CHUNK_MAX || n>image_.size-received_)return RISC_BANK_INVALID;
  if(timedOut())return fail(RISC_BANK_TIMEOUT);
  if(!(app_?io_.writeApp(io_.context,data,n):writeRaw(0,received_,data,n)) || !io_.hashAdd(io_.context,data,n))return fail(RISC_BANK_IO);
  received_+=n;return RISC_BANK_OK;
}
int32_t Transaction::finish(uint64_t token){
  if(!live(token) || state_!=RISC_BANK_RECEIVING || received_!=image_.size)return RISC_BANK_STATE;
  if(timedOut())return fail(RISC_BANK_TIMEOUT);
  uint8_t digest[32];if(!io_.hashEnd(io_.context,digest))return fail(RISC_BANK_IO);
  if(memcmp(digest,image_.sha256,32))return fail(RISC_BANK_INTEGRITY);
  if(app_ && !io_.finishApp(io_.context,target_))return fail(RISC_BANK_INTEGRITY);
  if(!io_.cleanup(io_.context))return fail(RISC_BANK_RETAINED);
  if(!hashStart())return fail(RISC_BANK_IO);
  state_=RISC_BANK_VERIFY_FIRMWARE;return RISC_BANK_OK;
}
int32_t Transaction::activate(uint64_t token){
  if(!live(token) || state_!=RISC_BANK_READY)return RISC_BANK_STATE;
  if(timedOut())return fail(RISC_BANK_TIMEOUT);
  if(!io_.cleanup(io_.context))return fail(RISC_BANK_RETAINED);
  /* set_boot_partition verifies app and writes redundant otadata last. */
  if(!io_.select(io_.context,target_)){
    state_=RISC_BANK_ACTIVATION_UNKNOWN;error_=RISC_BANK_RETAINED;return error_;
  }
  state_=RISC_BANK_ACTIVATED;return RISC_BANK_OK;
}
int32_t Transaction::abort(uint64_t token){
  if(!live(token) || state_==RISC_BANK_ACTIVATED || state_==RISC_BANK_ACTIVATION_UNKNOWN)return RISC_BANK_STATE;
  if(!io_.cleanup(io_.context))return fail(RISC_BANK_RETAINED);
  if(!io_.invalidate(io_.context,target_))return fail(RISC_BANK_IO);
  state_=RISC_BANK_IDLE;error_=0;token_=0;return RISC_BANK_OK;
}
}
