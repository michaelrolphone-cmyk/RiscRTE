#pragma once
#include <RiscRetainedWakeV1.h>
#include <cstddef>
#include <cstring>
#include <atomic>
namespace RiscRetainedWake {
/* Native-owned RTC representation: no addresses, heap ownership or raw SDK ABI.
 * Exact canonical metadata, not app-provided identity or a short hash. */
struct Identity { char app[96],version[64],cohort[512]; };
struct Image {
 uint32_t magic,format;
 Identity identity;
 risc_retained_wake_record_v1 record;
 uint32_t checksum;
};
static_assert(sizeof(Image)==828,"RTC checkpoint layout");
inline uint32_t checksum(const Image& image){
 uint32_t crc=~uint32_t(0);const auto* bytes=reinterpret_cast<const uint8_t*>(&image);
 for(size_t i=offsetof(Image,format);i<offsetof(Image,checksum);++i){
  crc^=bytes[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&uint32_t(-int32_t(crc&1)));
 }return ~crc;
}
inline bool valid(const risc_retained_wake_record_v1& r){
 return r.struct_size==sizeof(r) && r.type && r.schema_version && r.size && r.size<=RISC_RETAINED_WAKE_PAYLOAD_MAX;
}
class Store {
 public:
 explicit Store(Image& rtc):rtc_(rtc){}
 Store(const Store&)=delete;Store& operator=(const Store&)=delete;
 // Called once by boot owner. Always invalidate RTC before exposing a snapshot.
 void boot(uint32_t cause){
  if(booted_)return;
  booted_=true;cause_=cause;recovered_={};pending_={};
  if(cause>=RISC_BOOT_DEEP_TIMER && cause<=RISC_BOOT_DEEP_OTHER &&
     rtc_.magic==Magic && rtc_.format==1 && valid(rtc_.record) && rtc_.checksum==checksum(rtc_))recovered_=rtc_;
  rtc_={};
 }
 bool ready()const{return booted_;}
 int32_t read(const Identity& id,uint32_t type,uint32_t schema,risc_retained_wake_record_v1& out,uint32_t& cause){
  cause=cause_;
  if(!recovered_.magic || memcmp(&id,&recovered_.identity,sizeof(id)))return RISC_RETAINED_WAKE_ABSENT;
  if(recovered_.record.type!=type || recovered_.record.schema_version!=schema)return RISC_RETAINED_WAKE_MISMATCH;
  out=recovered_.record;recovered_={};return RISC_RETAINED_WAKE_OK;
 }
 void stage(const Identity& id,const risc_retained_wake_record_v1& r){
  if(!valid(r))return;
  pending_={};pending_.identity=id;pending_.format=1;
  pending_.record={sizeof(r),r.type,r.schema_version,r.size,{}};
  memcpy(pending_.record.payload,r.payload,r.size);
 }
 void clear(const Identity& id){cancel();if(!memcmp(&id,&recovered_.identity,sizeof(id)))recovered_={};}
 void cancel(){pending_={};rollback();}
 void rollback(){rtc_={};}
 // Called only after the native sleep owner passes all admission/arm checks.
 void commit(){
  rtc_={};if(!valid(pending_.record))return;
  pending_.checksum=checksum(pending_);rtc_=pending_;
  // Magic is the final commit marker. A reset/partial write cannot restore it.
  std::atomic_thread_fence(std::memory_order_seq_cst);
  *static_cast<volatile uint32_t*>(&rtc_.magic)=Magic;
 }
 private:
 static constexpr uint32_t Magic=0x52574b31;
 Image& rtc_;Image recovered_{},pending_{};uint32_t cause_=RISC_BOOT_RESET;bool booted_=false;
};
}
