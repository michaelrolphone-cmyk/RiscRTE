#pragma once
#include "NativeBaselineAdmission.h"
#include "CheckedFreshInitArtifacts.h"
#include <nvs.h>
#include <cstdio>
#include <climits>
namespace RiscExperimental {
// Disconnected, read-only interpretation of the shipped rte/kvj/kvd protocol.
// A held GLOBAL native lease must exclude every writer for the complete call.
// This is not SD-source selection, a semantic resolver, or migration cleanup.
class CheckedNvsLegacySources final {
 public:
 using C=NativeReplicaCore;using A=NativeBaselineAdmission;
 static constexpr uint32_t UnanchoredNativeV1=0x4e4c0001;
 static constexpr uint32_t MapMax=128*1024,SourceMax=3*MapMax+4096;
 enum class Reason {None,Absent,InvalidSource,UnknownSource,ExactSdBaseRequired,NativeArtifacts,SourceChanged,Capacity};
 struct Hooks {
  void*context;
  bool(*workerWithLease)(void*);
  uint64_t(*leaseEpoch)(void*);
  esp_err_t(*initializationStatus)(void*);
  int(*observe)(void*);
 };
 // The 16-byte logical owner is a stable firmware domain; legacyOwner is the
 // exact historical owner string (for example shared-00000006), never a label
 // inferred from the contents. Limits are trusted owner policy, not NVS sizes.
 CheckedNvsLegacySources(uint32_t ns,const uint8_t owner[16],const char*legacyOwner,
                         uint32_t mapCapacity,CheckedFreshInitArtifacts&artifacts,
                         A::Digest digest,Hooks hooks,C::Allocator allocator=C::heap())
  :ns_(ns),capacity_(mapCapacity),artifacts_(artifacts),hash_(digest),hooks_(hooks),alloc_(allocator){
  if(owner)std::memcpy(owner_,owner,16);
  if(validOwner(legacyOwner))std::strcpy(legacyOwner_,legacyOwner);
 }
 ~CheckedNvsLegacySources(){C::wipe(owner_,sizeof owner_);C::wipe(legacyOwner_,sizeof legacyOwner_);}
 CheckedNvsLegacySources(const CheckedNvsLegacySources&)=delete;
 CheckedNvsLegacySources&operator=(const CheckedNvsLegacySources&)=delete;
 Reason reason()const{return reason_;}
 bool retained()const{return retained_||artifacts_.retained();}
 bool exitSafe()const{return !entered_&&!retained()&&!opened_&&!iterator_&&artifacts_.exitSafe();}
 int inventory(A::Witness*out){return collect(ns_,nullptr,nullptr,out);}
 A::Sources sources(){return {this,
  [](void*p){return static_cast<CheckedNvsLegacySources*>(p)->observation();},nullptr,
  [](void*p,uint32_t ns,const A::Selection&s,const A::Rows&r,A::Witness*out){return static_cast<CheckedNvsLegacySources*>(p)->collect(ns,&s,&r,out);},
  [](void*p,uint32_t ns,const A::Witness&w){auto&s=*static_cast<CheckedNvsLegacySources*>(p);A::Witness now{};int rc=s.collect(ns,nullptr,nullptr,&now);return rc?rc:same(w,now)?int(C::Ok):s.fail(C::Conflict,Reason::SourceChanged);}};}
 private:
 uint32_t ns_,capacity_;CheckedFreshInitArtifacts&artifacts_;A::Digest hash_;Hooks hooks_;C::Allocator alloc_;
 uint8_t owner_[16]{};char legacyOwner_[96]{};Reason reason_=Reason::None;
 bool entered_=false,retained_=false,opened_=false;nvs_handle_t handle_=0;nvs_iterator_t iterator_=nullptr;uint64_t epoch_=0;
 struct Entry {bool&v;explicit Entry(bool&b):v(b){v=true;}~Entry(){v=false;}};
 struct Wipe {void*p;size_t n;~Wipe(){C::wipe(p,n);}};
 struct Buffer {
  C::Allocator a;uint8_t*p=nullptr;uint32_t cap=0,size=0,count=0;
  ~Buffer(){if(p){C::wipe(p,cap);a.release(a.context,p);}}
  bool create(uint32_t n){cap=n;p=static_cast<uint8_t*>(a.allocate(a.context,n));if(p)std::memset(p,0,n);return p!=nullptr;}
  void clear(){C::wipe(p,size);size=count=0;}
 };
 static uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 static uint64_t wide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
 static void put(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
 static uint32_t crc(const uint8_t*p,uint32_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
 static bool nonzero(const uint8_t*p,size_t n){uint8_t v=0;while(n--)v|=*p++;return v;}
 static bool key(const char*s){if(!s||!*s)return false;for(unsigned i=0;i<16;++i){const char c=s[i];if(!c)return true;if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'))return false;}return false;}
 static bool validOwner(const char*s){if(!s||!*s)return false;for(unsigned i=0;i<96;++i){const char c=s[i];if(!c)return true;if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'))return false;}return false;}
 static bool same(const A::Witness&a,const A::Witness&b){return a.namespaceId==b.namespaceId&&a.root==b.root&&a.kind==b.kind&&a.revision==b.revision&&!std::memcmp(a.fingerprint,b.fingerprint,32);}
 int fail(int rc,Reason why){reason_=why;return rc;}
 int retain(){retained_=true;return C::Retained;}
 int observation(){if(retained())return retain();if(!hooks_.observe)return C::Invalid;const int rc=hooks_.observe(hooks_.context);return rc==C::Retained?retain():rc;}
 int check(){
  int rc=observation();if(rc)return rc;
  const bool held=hooks_.workerWithLease&&hooks_.workerWithLease(hooks_.context);
  const uint64_t epoch=hooks_.leaseEpoch?hooks_.leaseEpoch(hooks_.context):0;
  const esp_err_t initialized=hooks_.initializationStatus?hooks_.initializationStatus(hooks_.context):ESP_FAIL;
  rc=observation();if(rc)return rc;
  const bool still=hooks_.workerWithLease&&hooks_.workerWithLease(hooks_.context);
  const uint64_t after=hooks_.leaseEpoch?hooks_.leaseEpoch(hooks_.context):0;
  rc=observation();if(rc)return rc;
  if(!held||!still)return opened_||iterator_?retain():int(C::Busy);
  if(!epoch||epoch!=after||(epoch_&&epoch!=epoch_))return opened_||iterator_?retain():int(C::Stale);
  if(initialized!=ESP_OK)return C::Io;
  return observation();
 }
 int cleanup(int result){
  if(opened_||iterator_){int rc=check();if(rc)return retain();
   if(iterator_){nvs_release_iterator(iterator_);iterator_=nullptr;rc=check();if(rc)return rc==C::Retained?rc:retain();}
   if(opened_){nvs_close(handle_);opened_=false;handle_=0;rc=check();if(rc==C::Retained)return rc;if(!result)result=rc;}
  }
  const int rc=check();return rc==C::Retained?rc:result?result:rc;
 }
 int open(const char*name){
  int rc=check();if(rc)return rc;
  const esp_err_t got=nvs_open(name,NVS_READONLY,&handle_);opened_=got==ESP_OK;
  rc=check();if(rc)return rc;
  return got==ESP_OK?int(C::Ok):got==ESP_ERR_NVS_NOT_FOUND?int(C::Missing):int(C::Io);
 }
 // Canonical raw framing: source ordinal, zero-padded key, length, EXACT value.
 // Sorting makes the evidence independent of NVS physical enumeration order.
 int captureRow(Buffer&b,uint32_t source,const char*k,const uint8_t*v,uint32_t n){
  const uint32_t need=24+n;if(need>b.cap-b.size)return fail(C::NoSpace,Reason::Capacity);
  uint32_t at=0;while(at<b.size){const auto*p=b.p+at;const int cmp=source==word(p)?std::strcmp(k,reinterpret_cast<const char*>(p+4)):(source<word(p)?-1:1);if(!cmp)return fail(C::Conflict,Reason::InvalidSource);if(cmp<0)break;at+=24+word(p+20);}
  std::memmove(b.p+at+need,b.p+at,b.size-at);auto*p=b.p+at;std::memset(p,0,24);put(p,source);std::strcpy(reinterpret_cast<char*>(p+4),k);put(p+20,n);std::memcpy(p+24,v,n);b.size+=need;++b.count;return C::Ok;
 }
 int scanSpace(Buffer&b,unsigned source,uint32_t*present){
  static const char*const prefixes[]={"rte","kvj","kvd","rnr"};
  char name[12]{};std::snprintf(name,sizeof name,"%s%08x",prefixes[source],unsigned(ns_));
  int rc=open(name);if(rc==C::Missing)return C::Ok;if(rc)return cleanup(rc);*present|=1u<<source;
  size_t before=0,after=0;esp_err_t got=nvs_get_used_entry_count(handle_,&before);rc=check();if(rc||got!=ESP_OK)return cleanup(rc?rc:int(C::Io));
  if(source==3){return cleanup(before?fail(C::Conflict,Reason::NativeArtifacts):int(C::Ok));}
  iterator_=nvs_entry_find("nvs",name,NVS_TYPE_ANY);rc=check();if(rc)return cleanup(rc);
  if(!iterator_&&before)return cleanup(C::Io);
  uint8_t value[C::ValueMax+8]{};Wipe wipe{value,sizeof value};uint32_t rows=0;
  while(iterator_){
   nvs_entry_info_t info{};nvs_entry_info(iterator_,&info);rc=check();if(rc)return cleanup(rc);
   if(!std::memchr(info.namespace_name,0,sizeof info.namespace_name)||!std::memchr(info.key,0,sizeof info.key)||std::strcmp(info.namespace_name,name)||info.type!=NVS_TYPE_BLOB||!key(info.key))return cleanup(fail(C::Io,Reason::InvalidSource));
   if(source==1&&std::strcmp(info.key,"snapshot")&&std::strcmp(info.key,"state2"))return cleanup(fail(C::Conflict,Reason::UnknownSource));
   size_t n=0;got=nvs_get_blob(handle_,info.key,nullptr,&n);rc=check();if(rc)return cleanup(rc);
   const uint32_t max=source==2?C::ValueMax+8:source==1?1472:C::ValueMax;
   if(got!=ESP_OK||!n||n>max)return cleanup(fail(C::Io,Reason::InvalidSource));
   size_t actual=n;got=nvs_get_blob(handle_,info.key,value,&actual);rc=check();if(rc)return cleanup(rc);
   if(got!=ESP_OK||actual!=n)return cleanup(fail(C::Io,Reason::InvalidSource));
   rc=captureRow(b,source,info.key,value,uint32_t(n));C::wipe(value,sizeof value);if(rc)return cleanup(rc);++rows;
   iterator_=nvs_entry_next(iterator_);rc=check();if(rc)return cleanup(rc);
  }
  got=nvs_get_used_entry_count(handle_,&after);rc=check();
  if(rc||got!=ESP_OK)return cleanup(rc?rc:int(C::Io));
  // Used entries count physical NVS slots, NOT keys. Never equate the two.
  if(before!=after||(before==0)!=(rows==0))return cleanup(fail(C::Conflict,Reason::SourceChanged));
  return cleanup(C::Ok);
 }
 int capture(Buffer&raw,uint32_t*present){
  raw.clear();*present=0;
  for(unsigned source=0;source<4;++source){const int rc=scanSpace(raw,source,present);if(rc)return rc;}
  int rc=check();if(rc)return rc;rc=artifacts_.inspectAbsent();const int after=check();
  if(after==C::Retained||rc==C::Retained)return retain();
  if(rc)return fail(rc,Reason::NativeArtifacts);
  return after;
 }
 int assign(Buffer&map,const char*k,const uint8_t*v,uint32_t n){
  if(!key(k)||!n||n>C::ValueMax)return fail(C::Io,Reason::InvalidSource);
  uint32_t at=0,old=0;while(at<map.size){const int cmp=std::strcmp(k,reinterpret_cast<const char*>(map.p+at));if(cmp<=0){if(!cmp)old=20+word(map.p+at+16);break;}at+=20+word(map.p+at+16);}
  const uint32_t need=20+n;if(need>map.cap-(map.size-old))return fail(C::NoSpace,Reason::Capacity);
  std::memmove(map.p+at+need,map.p+at+old,map.size-at-old);auto*p=map.p+at;std::memset(p,0,20);std::strcpy(reinterpret_cast<char*>(p),k);put(p+16,n);std::memcpy(p+20,v,n);const uint32_t prior=map.size;map.size=map.size-old+need;if(prior>map.size)C::wipe(map.p+map.size,prior-map.size);if(!old)++map.count;return C::Ok;
 }
 int journal(Buffer&map,const char*k,const uint8_t*p,uint32_t n){
  const bool v2=!std::strcmp(k,"state2");
  const uint32_t minimum=v2?132:128;
  if(n<minimum||std::memcmp(p,v2?"RISCKV2":"RISCKV1",8)||word(p+8)!=ns_||!std::memchr(p+24,0,96)||std::strcmp(reinterpret_cast<const char*>(p+24),legacyOwner_)||word(p+n-4)!=crc(p,n-4))return fail(C::Io,Reason::InvalidSource);
  if(v2){if(n!=132||word(p+12)||word(p+120)||word(p+124)!=132)return fail(C::Io,Reason::InvalidSource);return fail(C::Conflict,Reason::ExactSdBaseRequired);}
  const uint32_t count=word(p+120);if(word(p+12)>1||count>16||n!=128+count*84||(word(p+12)&&count))return fail(C::Io,Reason::InvalidSource);
  if(word(p+12))return fail(C::Conflict,Reason::ExactSdBaseRequired);
  for(uint32_t i=0;i<count;++i){const auto*e=p+124+i*84;if(!std::memchr(e,0,16)||!key(reinterpret_cast<const char*>(e))||!word(e+16)||word(e+16)>64)return fail(C::Io,Reason::InvalidSource);
   for(uint32_t j=0;j<i;++j)if(!std::strcmp(reinterpret_cast<const char*>(e),reinterpret_cast<const char*>(p+124+j*84)))return fail(C::Conflict,Reason::InvalidSource);
   int rc=assign(map,reinterpret_cast<const char*>(e),e+20,word(e+16));if(rc)return rc;
  }
  return C::Ok;
 }
 int decode(const Buffer&raw,Buffer&map){
  if(!raw.count)return fail(C::Missing,Reason::Absent);
  // This is the SHIPPED native format's precedence: original rows, full v1
  // journal rows, then latest per-key v2 deltas in their separate epoch. It
  // does not compare/merge independent SD datasets or manufacture user intents.
  for(uint32_t at=0;at<raw.size;){const auto*p=raw.p+at;const auto*k=reinterpret_cast<const char*>(p+4);const uint32_t n=word(p+20),source=word(p);const auto*v=p+24;int rc=0;
   if(source==0)rc=assign(map,k,v,n);
   else if(source==1)rc=journal(map,k,v,n);
   else {
    if(n<=8||!wide(v))return fail(C::Io,Reason::InvalidSource);
    // A successful production put advances the namespace generation once.
    // Equal generations for different surviving keys cannot prove one history.
    for(uint32_t other=0;other<at;other+=24+word(raw.p+other+20))if(word(raw.p+other)==2&&wide(raw.p+other+24)==wide(v))return fail(C::Conflict,Reason::InvalidSource);
    rc=assign(map,k,v+8,n-8);
   }
   if(rc)return rc;
   at+=24+n;
  }
  return C::Ok;
 }
 int digest(const Buffer&raw,uint32_t present,A::Witness*out){
  uint8_t prefix[144]{};Wipe wipe{prefix,sizeof prefix};std::memcpy(prefix,"RTENLV1",8);put(prefix+8,ns_);put(prefix+12,UnanchoredNativeV1);std::memcpy(prefix+16,owner_,16);std::memcpy(prefix+32,legacyOwner_,96);put(prefix+128,present);put(prefix+132,raw.count);put(prefix+136,raw.size);put(prefix+140,1);
  int rc=check();if(rc)return rc;bool ok=hash_.begin(hash_.context);rc=check();if(rc)return rc;if(!ok)return C::Io;
  ok=hash_.update(hash_.context,prefix,sizeof prefix);rc=check();if(rc)return rc;if(!ok)return C::Io;
  for(uint32_t at=0;at<raw.size;){uint32_t n=raw.size-at;if(n>4096)n=4096;ok=hash_.update(hash_.context,raw.p+at,n);rc=check();if(rc)return rc;if(!ok)return C::Io;at+=n;}
  A::Witness w{};w.namespaceId=ns_;w.root=A::RootState::NeverInitialized;w.kind=UnanchoredNativeV1;w.revision=1;
  ok=hash_.finish(hash_.context,w.fingerprint);rc=check();if(rc)return rc;if(!ok||!nonzero(w.fingerprint,32))return C::Io;*out=w;return C::Ok;
 }
 int verify(Buffer&raw,const A::Witness&w){uint32_t present=0;int rc=capture(raw,&present);A::Witness now{};if(!rc)rc=digest(raw,present,&now);return rc?rc:same(w,now)?int(C::Ok):fail(C::Conflict,Reason::SourceChanged);}
 int collect(uint32_t ns,const A::Selection*selected,const A::Rows*rows,A::Witness*out){
  if(out)*out={};
  if(retained())return retain();
  if(entered_)return C::Busy;
  if(!out||ns!=ns_||!ns||ns>INT32_MAX||!capacity_||capacity_>MapMax||artifacts_.namespaceId()!=ns||!nonzero(owner_,sizeof owner_)||!legacyOwner_[0]||!hash_.begin||!hash_.update||!hash_.finish||!alloc_.allocate||!alloc_.release||!hooks_.workerWithLease||!hooks_.leaseEpoch||!hooks_.initializationStatus||!hooks_.observe||(selected&&(!rows||!rows->append)))return C::Invalid;
  Entry entry(entered_);reason_=Reason::None;epoch_=0;int rc=check();if(rc)return rc;epoch_=hooks_.leaseEpoch(hooks_.context);rc=check();if(rc)return rc;
  Buffer raw{alloc_},map{alloc_};const bool rawOk=raw.create(3*capacity_+4096);rc=check();if(rc)return rc;if(!rawOk)return C::NoMemory;
  const bool mapOk=map.create(capacity_);rc=check();if(rc)return rc;if(!mapOk)return C::NoMemory;
  uint32_t present=0;rc=capture(raw,&present);if(!rc)rc=decode(raw,map);A::Witness w{};if(!rc)rc=digest(raw,present,&w);if(!rc)rc=verify(raw,w);if(rc)return rc;
  if(selected){if(selected->kind!=w.kind||selected->revision!=w.revision||std::memcmp(selected->fingerprint,w.fingerprint,32))return fail(C::Conflict,Reason::SourceChanged);
   for(uint32_t at=0;at<map.size;){rc=check();if(rc)return rc;const uint32_t n=word(map.p+at+16);rc=rows->append(rows->context,reinterpret_cast<const char*>(map.p+at),map.p+at+20,n);const int after=check();if(after==C::Retained||rc==C::Retained)return retain();if(rc||after)return rc?rc:after;at+=20+n;}
   rc=verify(raw,w);if(rc)return rc;
  }
  rc=check();if(!rc)*out=w;return rc;
 }
};
}
