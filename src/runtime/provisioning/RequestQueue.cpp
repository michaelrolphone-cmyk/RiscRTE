#include "RequestQueue.h"
#include <cstring>
#include <memory>
namespace RiscProvision {namespace {
const uint8_t magic[8]={'R','T','E','P','R','Q','1',0};
uint32_t get(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
void put(uint8_t*p,uint32_t n){for(uint32_t i=0;i<4;++i)p[i]=uint8_t(n>>(i*8));}
bool digest(uint8_t*p,uint32_t n,uint8_t*out,RequestHash hash,uint32_t offset=128){
 uint8_t saved[32];memcpy(saved,p+offset,32);memset(p+offset,0,32);
 const bool ok=hash(p,n,out);memcpy(p+offset,saved,32);return ok;
}
bool persist(RequestTransport t,const char*key,const void* bytes,uint32_t size,void* scratch,uint32_t cap){
 if(!t.set(t.input.context,key,bytes,size)||!t.commit(t.input.context))return false;
 uint32_t actual=0;
 return t.input.read(t.input.context,key,scratch,cap,&actual)==InputStatus::Ready&&actual==size&&!memcmp(scratch,bytes,size);
}
bool remove(RequestTransport t,const char*key){
 return t.erase(t.input.context,key)&&t.commit(t.input.context)&&t.probe(t.input.context,key)==InputStatus::Missing;
}
InputStatus stateValue(Input input,uint8_t&value){
 uint32_t n=0;value=0;auto result=input.read(input.context,"state",&value,1,&n);
 if(result==InputStatus::Ready&&(n!=1||(value!=RequestReady&&value!=RequestActive)))return InputStatus::Invalid;
 return result;
}
bool setState(RequestTransport t,uint8_t wanted){
 uint8_t value=0;auto state=stateValue(t.input,value);
 if(state==InputStatus::Ready&&value==wanted)return true;
 if(state!=InputStatus::Missing&&state!=InputStatus::Ready)return false;
 if(!t.set(t.input.context,"state",&wanted,1)||!t.commit(t.input.context))return false;
 return stateValue(t.input,value)==InputStatus::Ready&&value==wanted;
}
bool terminalReceipt(const RequestView&old,const RequestSource&source,uint8_t(&out)[RequestReceiptBytes],RequestHash hash){
 memset(out,0,sizeof(out));memcpy(out,magic,8);put(out+8,2);put(out+12,sizeof(out));
 put(out+16,old.profileSize);put(out+20,old.timeSize);put(out+24,old.source.storeAbi);put(out+28,old.source.firmwareSize);
 memcpy(out+32,old.source.storeSha,32);memcpy(out+64,old.source.firmwareSha,32);
 memcpy(out+96,old.profileSha,32);memcpy(out+128,old.requestSha,32);memcpy(out+160,old.targetStoreSha,32);
 memcpy(out+192,source.storeSha,32);memcpy(out+224,old.timeSha,32);
 uint8_t checksum[32];if(!hash(out,sizeof(out),checksum))return false;
 memcpy(out+256,checksum,32);return true;
}
}
InputStatus readRequest(Input input,void*bytes,uint32_t cap,uint32_t*size,bool*controlled){
 if(controlled)*controlled=false;if(size)*size=0;
 if(!input.read||!bytes||!size)return InputStatus::Invalid;
 auto state=input.read(input.context,"receipt",bytes,cap,size);
 if(state==InputStatus::Ready){
  if(*size!=RequestReceiptBytes||get(static_cast<uint8_t*>(bytes)+8)!=2)return InputStatus::Invalid;
  if(controlled)*controlled=true;return state;
 }
 if(state!=InputStatus::Missing)return state;
 uint8_t marker=0;const auto markerState=stateValue(input,marker);
 if(markerState==InputStatus::Ready&&marker==RequestActive)return InputStatus::Invalid;
 if(markerState!=InputStatus::Missing&&markerState!=InputStatus::Ready)return markerState;
 state=input.read(input.context,"request",bytes,cap,size);
 // Once a valid queue has been acknowledged, missing records cannot restore
 // legacy bootstrap input. ACTIVE also fences a terminal self-erased by NVS.
 return state==InputStatus::Missing&&markerState==InputStatus::Ready?InputStatus::Invalid:state;
}
bool parseRequest(void*bytes,uint32_t n,RequestView&out,RequestHash hash){
 out={};if(!bytes||!hash||n<RequestHeaderBytes||n>RequestRecordBytes)return false;
 auto*p=static_cast<uint8_t*>(bytes);const uint32_t version=get(p+8),profile=get(p+16),time=get(p+20);
 if(memcmp(p,magic,8)||get(p+12)!=n||!profile||profile>ProfileInputBytes||!time||time>384)return false;
 if(version==1 ? RequestHeaderBytes+profile+time!=n : version!=2||n!=RequestReceiptBytes)return false;
 const bool terminal=version==2;uint8_t actual[32];const uint32_t checksum=terminal?256:128;
 if(!digest(p,n,actual,hash,checksum)||memcmp(actual,p+checksum,32))return false;
 if(!terminal){
  if(!hash(p+RequestHeaderBytes,profile,actual)||memcmp(actual,p+96,32))return false;
  auto parsed=std::unique_ptr<Profile>(new(std::nothrow) Profile);
  if(!parsed||!inspectInstallInput(p+RequestHeaderBytes,profile,p+RequestHeaderBytes+profile,time,*parsed))return false;
  if(parsed->imageMode())memcpy(out.targetStoreSha,parsed->image.sha256,32);
  out.profile=p+RequestHeaderBytes;out.time=out.profile+profile;
  if(!hash(out.time,time,out.timeSha))return false;
 }else{
  memcpy(out.targetStoreSha,p+160,32);memcpy(out.consumedStoreSha,p+192,32);memcpy(out.timeSha,p+224,32);
 }
 out.terminal=terminal;out.profileSize=profile;out.timeSize=time;
 out.source.storeAbi=get(p+24);out.source.firmwareSize=get(p+28);if(!out.source.storeAbi||!out.source.firmwareSize){out={};return false;}
 memcpy(out.source.storeSha,p+32,32);memcpy(out.source.firmwareSha,p+64,32);memcpy(out.profileSha,p+96,32);memcpy(out.requestSha,p+128,32);return true;
}
bool requestConsumed(const RequestView&r,const RequestSource&s){
 if(!s.receiptAvailable||memcmp(r.profileSha,s.consumedProfileSha,32)||r.source.storeAbi!=s.storeAbi||
    r.source.firmwareSize!=s.firmwareSize||memcmp(r.source.firmwareSha,s.firmwareSha,32))return false;
 if(r.terminal)return !memcmp(r.consumedStoreSha,s.storeSha,32);
 // Image profiles identify their complete target directly. Legacy per-file
 // requests have no target-image digest; their confirmed bank receipt remains
 // the proof. Neither case may compact against another native firmware.
 const uint8_t absent[32]{};
 return !memcmp(r.targetStoreSha,absent,32)||!memcmp(r.targetStoreSha,s.storeSha,32);
}
bool requestSourceMatches(const RequestView&r,const RequestSource&s){return !r.terminal&&r.source.storeAbi==s.storeAbi&&r.source.firmwareSize==s.firmwareSize&&!memcmp(r.source.storeSha,s.storeSha,32)&&!memcmp(r.source.firmwareSha,s.firmwareSha,32);}
int32_t queueRequest(RequestTransport t,const risc_provision_request_v1&q,const RequestSource&s,void*oldBytes,void*nextBytes,uint32_t cap,RequestHash hash,uint8_t out[32]){
 if(out)memset(out,0,32);
 if(!out||!oldBytes||!nextBytes||oldBytes==nextBytes||cap<RequestRecordBytes||!hash||!t.input.read||!t.set||!t.commit||!t.erase||!t.probe)return RISC_PROVISION_REQUEST_UNAVAILABLE;
 if(q.struct_size<sizeof(q)||!q.time_size||!validInstallInput(q.profile_json,q.profile_size,q.time_json,q.time_size))return RISC_PROVISION_REQUEST_INVALID;
 if(memcmp(q.expected_active_store_sha256,s.storeSha,32))return RISC_PROVISION_REQUEST_STALE;
 // Copy the complete next request before any persistence callback. Compaction
 // never borrows caller credentials or reuses its readback buffer as input.
 auto*p=static_cast<uint8_t*>(nextBytes);const uint32_t n=RequestHeaderBytes+q.profile_size+q.time_size;
 memset(p,0,RequestHeaderBytes);memcpy(p,magic,8);put(p+8,1);put(p+12,n);put(p+16,q.profile_size);put(p+20,q.time_size);put(p+24,s.storeAbi);put(p+28,s.firmwareSize);
 memcpy(p+32,s.storeSha,32);memcpy(p+64,s.firmwareSha,32);memcpy(p+RequestHeaderBytes,q.profile_json,q.profile_size);memcpy(p+RequestHeaderBytes+q.profile_size,q.time_json,q.time_size);
 uint8_t timeSha[32],sum[32];
 if(!hash(p+RequestHeaderBytes,q.profile_size,p+96)||!hash(p+RequestHeaderBytes+q.profile_size,q.time_size,timeSha)||!digest(p,n,sum,hash))return RISC_PROVISION_REQUEST_IO;
 memcpy(p+128,sum,32);
 uint32_t oldSize=0;bool controlled=false;auto state=readRequest(t.input,oldBytes,cap,&oldSize,&controlled);RequestView old;
 if(state==InputStatus::Ready){
  if(!parseRequest(oldBytes,oldSize,old,hash))return RISC_PROVISION_REQUEST_INVALID;
  const bool consumed=requestConsumed(old,s);
  if(old.profileSize==q.profile_size&&old.timeSize==q.time_size&&!memcmp(old.profileSha,p+96,32)&&!memcmp(old.timeSha,timeSha,32)&&
     (requestSourceMatches(old,s)||consumed)){
   if(!old.terminal&&!setState(t,RequestReady))return RISC_PROVISION_REQUEST_UNKNOWN;
   memcpy(out,old.requestSha,32);return RISC_PROVISION_REQUEST_OK;
  }
  if(!consumed)return RISC_PROVISION_REQUEST_BUSY;
  if(!controlled){
   // The separate terminal is authoritative before old payload removal.
   // Native storage uses a single-page string, never multipart blob update.
   uint8_t receipt[RequestReceiptBytes];
   if(!terminalReceipt(old,s,receipt,hash))return RISC_PROVISION_REQUEST_IO;
   if(!persist(t,"receipt",receipt,sizeof(receipt),oldBytes,cap))return RISC_PROVISION_REQUEST_UNKNOWN;
  }
  if(!setState(t,RequestActive))return RISC_PROVISION_REQUEST_UNKNOWN;
  // A payload hidden by this matching terminal is either consumed or staged
  // and unadmitted. Do not read it: an interrupted erase may be incomplete.
  if(!remove(t,"request"))return RISC_PROVISION_REQUEST_UNKNOWN;
 }else if(state!=InputStatus::Missing)return RISC_PROVISION_REQUEST_UNAVAILABLE;
 if(!persist(t,"request",p,n,oldBytes,cap))return RISC_PROVISION_REQUEST_UNKNOWN;
 if(!setState(t,RequestReady))return RISC_PROVISION_REQUEST_UNKNOWN;
 // Publishing the fresh pending request is the final durable selection step.
 // A cut before terminal removal continues to report the old consumed receipt.
 if(state==InputStatus::Ready&&!remove(t,"receipt"))return RISC_PROVISION_REQUEST_UNKNOWN;
 memcpy(out,sum,32);return RISC_PROVISION_REQUEST_OK;
}
Input requestInput(RequestView&r){return {&r,[](void*context,const char*key,void*out,uint32_t cap,uint32_t*n){
 if(n)*n=0;
 if(!key||!out||!n)return InputStatus::Invalid;
 auto&v=*static_cast<RequestView*>(context);
 // Terminal receipts deliberately provide no bootstrap profile/time. Present
 // receipt state must never be interpreted as absence and replay owner input.
 if(v.terminal)return InputStatus::Unavailable;
 const void*p=nullptr;uint32_t size=0;
 static const char descriptor[]="{\"schema\":\"riscrte.bootstrap\",\"schema_version\":2,\"profile_key\":\"profile\",\"time_key\":\"time\"}";
 if(!strcmp(key,"descriptor")){p=descriptor;size=sizeof(descriptor)-1;}else if(!strcmp(key,"profile")){p=v.profile;size=v.profileSize;}else if(!strcmp(key,"time")){p=v.time;size=v.timeSize;}else return InputStatus::Missing;
 if(size>cap)return InputStatus::Invalid;
 memcpy(out,p,size);*n=size;return InputStatus::Ready;
 }};}
}
