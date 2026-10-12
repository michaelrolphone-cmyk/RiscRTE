#pragma once
#include "NativeBaselineAdmission.h"
#include "CheckedFreshInitArtifacts.h"
#include <nvs.h>
#include <cstdio>
namespace RiscExperimental {
// Read-only native source inventory for the existing NVS layout. This collector
// never initializes NVS, writes a key, interprets a legacy value, or consults SD.
// Activation still needs the real global native lease and qualified filesystem
// inspector. In particular, the current ESP inspector deliberately refuses.
class CheckedNvsFreshSources final {
 public:
 using C=NativeReplicaCore;using A=NativeBaselineAdmission;
 struct Hooks {
  void*context;
  bool(*workerWithLease)(void*);
  esp_err_t(*initializationStatus)(void*);
  int(*observe)(void*);
 };
 // A stable firmware-native logical owner, never an address, boot ID or secret.
 // The 16-byte domain is copied and permanently bound with the namespace/schema.
 CheckedNvsFreshSources(uint32_t ns,const uint8_t owner[16],CheckedFreshInitArtifacts&artifacts,Hooks hooks)
  :ns_(ns),artifacts_(artifacts),hooks_(hooks){if(owner)std::memcpy(owner_,owner,16);}
 CheckedNvsFreshSources(const CheckedNvsFreshSources&)=delete;
 CheckedNvsFreshSources&operator=(const CheckedNvsFreshSources&)=delete;
 ~CheckedNvsFreshSources(){C::wipe(owner_,sizeof owner_);}
 // RAM-only composition identity, not an absence or source receipt.
 bool boundTo(uint32_t ns,const CheckedFreshInitArtifacts&a)const{return ns_==ns && &artifacts_==&a;}
 bool retained()const{return retained_||artifacts_.retained();}
 bool exitSafe()const{return !entered_&&!retained()&&!opened_&&!iterator_&&artifacts_.exitSafe();}
 A::Sources sources(){return {this,
  [](void*p){return static_cast<CheckedNvsFreshSources*>(p)->observation();},
  [](void*p,uint32_t ns,A::Witness*out){return static_cast<CheckedNvsFreshSources*>(p)->collect(ns,out,nullptr,false);},nullptr,
  [](void*p,uint32_t ns,const A::Witness&w){auto&s=*static_cast<CheckedNvsFreshSources*>(p);A::Witness now{};const int rc=s.collect(ns,&now,nullptr,true);return rc?rc:s.same(w,now)?int(C::Ok):int(C::Conflict);},
  A::FreshAbsenceV1,
  [](void*p,uint32_t ns,const C::FreshInitReceipt&r,A::Witness*out){return static_cast<CheckedNvsFreshSources*>(p)->collect(ns,out,&r,true);}};}
 private:
 friend class CheckedSdNativeRestore;
 // Private bridge: the checked restore controller computes the receipt digest
 // from this collector's original absence witness and exact checked SD image.
 struct SdCheck {const C::SdInitReceipt*receipt;const uint8_t*fingerprint;const uint8_t*image;uint32_t imageBytes;};
 int sdInventory(const SdCheck&sd,bool allowReceipt,A::Witness*out){return collect(ns_,out,nullptr,allowReceipt,&sd);}
 A::Witness sdOriginalWitness()const{return witness();}
 uint32_t ns_;CheckedFreshInitArtifacts&artifacts_;Hooks hooks_;uint8_t owner_[16]{};
 bool entered_=false,retained_=false,opened_=false;nvs_handle_t handle_=0;nvs_iterator_t iterator_=nullptr;
 struct Entry {bool&v;explicit Entry(bool&b):v(b){v=true;}~Entry(){v=false;}};
 struct Wipe {void*p;size_t n;~Wipe(){C::wipe(p,n);}};
 static uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 static uint64_t wide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
 static void put(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
 static uint32_t crc(const uint8_t*p,uint32_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
 static bool nonzero(const uint8_t*p,size_t n){uint8_t v=0;while(n--)v|=*p++;return v;}
 static bool same(const A::Witness&a,const A::Witness&b){return a.namespaceId==b.namespaceId&&a.root==b.root&&a.kind==b.kind&&a.revision==b.revision&&!std::memcmp(a.fingerprint,b.fingerprint,32);}
 int retain(){retained_=true;return C::Retained;}
 int observation(){if(retained())return retain();if(!hooks_.observe)return C::Invalid;const int rc=hooks_.observe(hooks_.context);return rc==C::Retained?retain():rc;}
 int check(){
  int rc=observation();if(rc)return rc;
  const bool held=hooks_.workerWithLease&&hooks_.workerWithLease(hooks_.context);
  rc=observation();if(rc)return rc;
  const bool stillHeld=hooks_.workerWithLease&&hooks_.workerWithLease(hooks_.context);
  rc=observation();if(rc)return rc;
  if(!held||!stillHeld)return opened_||iterator_?retain():int(C::Busy);
  if(!hooks_.initializationStatus||hooks_.initializationStatus(hooks_.context)!=ESP_OK)return C::Io;
  return observation();
 }
 int cleanup(int result){
  if(opened_||iterator_){int rc=check();if(rc==C::Retained)return rc;if(rc)return retain();
   if(iterator_){nvs_release_iterator(iterator_);iterator_=nullptr;rc=check();if(rc)return rc==C::Retained?rc:retain();}
   if(opened_){nvs_close(handle_);opened_=false;handle_=0;rc=check();if(rc==C::Retained)return rc;if(!result)result=rc;}
  }
  const int rc=check();return rc==C::Retained?rc:result?result:rc;
 }
 int open(const char*prefix){
  int rc=check();if(rc)return rc;if(opened_||iterator_)return C::Invalid;
  char name[12]{};std::snprintf(name,sizeof name,"%s%08x",prefix,unsigned(ns_));
  const esp_err_t got=nvs_open(name,NVS_READONLY,&handle_);opened_=got==ESP_OK;
  rc=check();if(rc)return rc;
  return got==ESP_OK?int(C::Ok):got==ESP_ERR_NVS_NOT_FOUND?int(C::Missing):int(C::Io);
 }
 int empty(const char*prefix){
  int rc=open(prefix);if(rc==C::Missing)return C::Ok;if(rc)return cleanup(rc);
  size_t used=0;const esp_err_t got=nvs_get_used_entry_count(handle_,&used);rc=check();
  return cleanup(rc?rc:got!=ESP_OK?int(C::Io):used?int(C::Conflict):int(C::Ok));
 }
 int blob(const char*key,uint8_t*out,size_t bytes,bool*found){
  *found=false;int rc=check();if(rc)return rc;size_t n=0;esp_err_t got=nvs_get_blob(handle_,key,nullptr,&n);rc=check();if(rc)return rc;
  if(got==ESP_ERR_NVS_NOT_FOUND)return C::Ok;
  if(got!=ESP_OK||n!=bytes)return C::Io;
  n=bytes;got=nvs_get_blob(handle_,key,out,&n);rc=check();if(rc)return rc;
  if(got!=ESP_OK||n!=bytes)return C::Io;
  *found=true;return C::Ok;
 }
 A::Witness witness()const{
  A::Witness w{};w.namespaceId=ns_;w.root=A::RootState::NeverInitialized;w.revision=1;
  // Exact, injective 32-byte encoding of schema/domain/namespace/owner. Every
  // source fact is checked absence (owned attempts normalize only below), so
  // there are no unencoded variable rows and no entropy/hash-service dependency.
  std::memcpy(w.fingerprint,"RTENFV1",8);put(w.fingerprint+8,ns_);put(w.fingerprint+12,1);std::memcpy(w.fingerprint+16,owner_,16);return w;
 }
 int metadata(bool allowReceipt,const C::FreshInitReceipt*receipt,bool*attempt,uint64_t*sequence,const SdCheck*sd=nullptr){
  *attempt=false;*sequence=0;int rc=open("rnr");if(rc==C::Missing)return (receipt||(sd&&sd->receipt))?int(C::Conflict):int(C::Ok);if(rc)return cleanup(rc);
  size_t before=0,after=0;esp_err_t got=nvs_get_used_entry_count(handle_,&before);rc=check();if(rc||got!=ESP_OK)return cleanup(rc?rc:int(C::Io));
  if(!before)return cleanup((receipt||(sd&&sd->receipt))?int(C::Conflict):int(C::Ok));
  if(!allowReceipt)return cleanup(C::Conflict);
  char name[12]{};std::snprintf(name,sizeof name,"rnr%08x",unsigned(ns_));iterator_=nvs_entry_find("nvs",name,NVS_TYPE_ANY);rc=check();
  if(rc||!iterator_)return cleanup(rc?rc:int(C::Io));
  unsigned mask=0;
  while(iterator_){
   nvs_entry_info_t info{};nvs_entry_info(iterator_,&info);rc=check();if(rc)return cleanup(rc);
   if(!std::memchr(info.namespace_name,0,sizeof info.namespace_name)||!std::memchr(info.key,0,sizeof info.key)||std::strcmp(info.namespace_name,name)||info.type!=NVS_TYPE_BLOB)return cleanup(C::Conflict);
   const unsigned bit=!std::strcmp(info.key,"root")?1:!std::strcmp(info.key,"next")?2:0;
   if(!bit||(mask&bit))return cleanup(C::Conflict);
   mask|=bit;
   iterator_=nvs_entry_next(iterator_);rc=check();if(rc)return cleanup(rc);
  }
  uint8_t root[48]{},next[24]{};Wipe wipeRoot{root,sizeof root},wipeNext{next,sizeof next};bool hasRoot=false,hasNext=false;
  rc=blob("root",root,sizeof root,&hasRoot);if(!rc)rc=blob("next",next,sizeof next,&hasNext);
  if(rc)return cleanup(rc);
  if(!hasRoot||mask!=(1u|(hasNext?2u:0u)))return cleanup(C::Conflict);
  const auto expected=witness();
  if(std::memcmp(root,sd?"RTENSI1":"RTENFI1",8)||word(root+8)!=ns_||std::memcmp(root+12,sd?sd->fingerprint:expected.fingerprint,32)||word(root+44)!=crc(root,44))return cleanup(C::Conflict);
  if(receipt&&(!receipt->valid()||receipt->namespaceId()!=ns_||std::memcmp(receipt->fingerprint(),root+12,32)))return cleanup(C::Conflict);
  if(sd&&sd->receipt&&(!sd->receipt->valid()||sd->receipt->namespaceId()!=ns_||std::memcmp(sd->receipt->fingerprint(),root+12,32)))return cleanup(C::Conflict);
  if(hasNext){if(std::memcmp(next,"RTENSEQ",8)||word(next+16)||word(next+20)!=crc(next,20)||!wide(next+8)||wide(next+8)>UINT64_MAX/2)return cleanup(C::Io);*sequence=wide(next+8);}
  got=nvs_get_used_entry_count(handle_,&after);rc=check();if(rc||got!=ESP_OK||before!=after)return cleanup(rc?rc:int(C::Conflict));
  rc=cleanup(C::Ok);if(!rc)*attempt=true;return rc;
 }
 void canonical(uint8_t*p)const{
  static_assert(C::HeaderV3==480&&C::ProvenanceBytes==96,"Fresh schema changed");
  std::memset(p,0,484);std::memcpy(p,"RTENR03",8);put(p+8,ns_);p[16]=1;put(p+108,484);
  std::memcpy(p+384,"RTENBP1",8);put(p+392,ns_);put(p+396,1);put(p+476,crc(p+384,92));put(p+480,crc(p,480));
 }
 int collect(uint32_t ns,A::Witness*out,const C::FreshInitReceipt*receipt,bool allowReceipt,const SdCheck*sd=nullptr){
  if(out)*out={};
  if(retained())return retain();
  if(entered_)return C::Busy;
  if(!out||ns!=ns_||!ns||ns>INT32_MAX||artifacts_.namespaceId()!=ns||!nonzero(owner_,sizeof owner_))return C::Invalid;
  Entry entered(entered_);int rc=check();if(rc)return rc;
  // Any native row, format-1 journal, format-2 anchor/delta or unknown key is a
  // competing source. Empty namespaces are harmless; no payload is read/logged.
  for(const char*prefix:{"rte","kvj","kvd"}){rc=empty(prefix);if(rc)return rc;}
  bool attempt=false;uint64_t sequence=0;rc=metadata(allowReceipt,receipt,&attempt,&sequence,sd);if(rc)return rc;
  if(attempt&&sequence&&sd)rc=artifacts_.inspectSdAttempt(sd->image,sd->imageBytes,sequence);
  else if(attempt&&sequence){uint8_t image[484]{};Wipe wipe{image,sizeof image};canonical(image);rc=artifacts_.inspectAttempt(image,sizeof image,sequence);}
  else rc=artifacts_.inspectAbsent();
  if(rc)return rc==C::Retained?retain():rc;
  // Recheck metadata and all competing sources after filesystem callbacks.
  bool current=false;uint64_t currentSequence=0;rc=metadata(allowReceipt,receipt,&current,&currentSequence,sd);if(rc)return rc;
  if(current!=attempt||currentSequence!=sequence)return C::Conflict;
  for(const char*prefix:{"rte","kvj","kvd"}){rc=empty(prefix);if(rc)return rc;}
  rc=check();if(rc)return rc;*out=witness();return C::Ok;
 }
};
}
