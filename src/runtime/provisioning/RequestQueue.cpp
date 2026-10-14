#include "RequestQueue.h"
#include <cstring>
#include <memory>
namespace RiscProvision {namespace {
const uint8_t magic[8]={'R','T','E','P','R','Q','1',0};
uint32_t get(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
void put(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(i*8));}
bool digest(uint8_t*p,uint32_t n,uint8_t*out,RequestHash hash){uint8_t saved[32];memcpy(saved,p+128,32);memset(p+128,0,32);bool ok=hash(p,n,out);memcpy(p+128,saved,32);return ok;}
}
bool parseRequest(void*bytes,uint32_t n,RequestView&out,RequestHash hash){
 out={};if(!bytes||!hash||n<RequestHeaderBytes||n>RequestRecordBytes)return false;
 auto*p=static_cast<uint8_t*>(bytes);uint32_t profile=get(p+16),time=get(p+20);
 if(memcmp(p,magic,8)||get(p+8)!=1||get(p+12)!=n||!profile||profile>ProfileInputBytes||!time||time>384||RequestHeaderBytes+profile+time!=n)return false;
 uint8_t actual[32];if(!digest(p,n,actual,hash)||memcmp(actual,p+128,32)||!hash(p+RequestHeaderBytes,profile,actual)||memcmp(actual,p+96,32))return false;
 auto parsed=std::unique_ptr<Profile>(new(std::nothrow) Profile);
 if(!parsed||!inspectInstallInput(p+RequestHeaderBytes,profile,p+RequestHeaderBytes+profile,time,*parsed))return false;
 if(parsed->imageMode())memcpy(out.targetStoreSha,parsed->image.sha256,32);
 out.profile=p+RequestHeaderBytes;out.profileSize=profile;out.time=out.profile+profile;out.timeSize=time;
 out.source.storeAbi=get(p+24);out.source.firmwareSize=get(p+28);if(!out.source.storeAbi||!out.source.firmwareSize)return false;
 memcpy(out.source.storeSha,p+32,32);memcpy(out.source.firmwareSha,p+64,32);memcpy(out.profileSha,p+96,32);memcpy(out.requestSha,p+128,32);return true;
}
bool requestConsumed(const RequestView&r,const RequestSource&s){return s.receiptAvailable&&!memcmp(r.profileSha,s.consumedProfileSha,32);}
bool requestSourceMatches(const RequestView&r,const RequestSource&s){return r.source.storeAbi==s.storeAbi&&r.source.firmwareSize==s.firmwareSize&&!memcmp(r.source.storeSha,s.storeSha,32)&&!memcmp(r.source.firmwareSha,s.firmwareSha,32);}
int32_t queueRequest(InstallTransport t,const risc_provision_request_v1&q,const RequestSource&s,void*oldBytes,void*nextBytes,uint32_t cap,RequestHash hash,uint8_t out[32]){
 if(out)memset(out,0,32);
 if(!out||!oldBytes||!nextBytes||oldBytes==nextBytes||cap<RequestRecordBytes||!hash||!t.input.read||!t.set||!t.commit)return RISC_PROVISION_REQUEST_UNAVAILABLE;
 if(q.struct_size<sizeof(q)||!q.time_size||!validInstallInput(q.profile_json,q.profile_size,q.time_json,q.time_size))return RISC_PROVISION_REQUEST_INVALID;
 if(memcmp(q.expected_active_store_sha256,s.storeSha,32))return RISC_PROVISION_REQUEST_STALE;
 uint32_t oldSize=0;auto state=t.input.read(t.input.context,"request",oldBytes,cap,&oldSize);RequestView old;
 if(state==InputStatus::Ready){
  if(!parseRequest(oldBytes,oldSize,old,hash))return RISC_PROVISION_REQUEST_INVALID;
  if(old.profileSize==q.profile_size&&old.timeSize==q.time_size&&!memcmp(old.profile,q.profile_json,q.profile_size)&&!memcmp(old.time,q.time_json,q.time_size)&&
     (requestSourceMatches(old,s)||requestConsumed(old,s))){memcpy(out,old.requestSha,32);return RISC_PROVISION_REQUEST_OK;}
  if(!requestConsumed(old,s))return RISC_PROVISION_REQUEST_BUSY;
 }else if(state!=InputStatus::Missing)return RISC_PROVISION_REQUEST_UNAVAILABLE;
 auto*p=static_cast<uint8_t*>(nextBytes);const uint32_t n=RequestHeaderBytes+q.profile_size+q.time_size;
 memset(p,0,RequestHeaderBytes);memcpy(p,magic,8);put(p+8,1);put(p+12,n);put(p+16,q.profile_size);put(p+20,q.time_size);put(p+24,s.storeAbi);put(p+28,s.firmwareSize);
 memcpy(p+32,s.storeSha,32);memcpy(p+64,s.firmwareSha,32);memcpy(p+RequestHeaderBytes,q.profile_json,q.profile_size);memcpy(p+RequestHeaderBytes+q.profile_size,q.time_json,q.time_size);
 if(!hash(q.profile_json,q.profile_size,p+96))return RISC_PROVISION_REQUEST_IO;
 uint8_t sum[32];if(!digest(p,n,sum,hash))return RISC_PROVISION_REQUEST_IO;memcpy(p+128,sum,32);
 if(!t.set(t.input.context,"request",p,n)||!t.commit(t.input.context))return RISC_PROVISION_REQUEST_UNKNOWN;
 uint32_t actual=0;if(t.input.read(t.input.context,"request",oldBytes,cap,&actual)!=InputStatus::Ready||actual!=n||memcmp(oldBytes,p,n))return RISC_PROVISION_REQUEST_UNKNOWN;
 memcpy(out,sum,32);return RISC_PROVISION_REQUEST_OK;
}
Input requestInput(RequestView&r){return {&r,[](void*context,const char*key,void*out,uint32_t cap,uint32_t*n){
 if(n)*n=0;
 if(!key||!out||!n)return InputStatus::Invalid;
 auto&v=*static_cast<RequestView*>(context);const void*p=nullptr;uint32_t size=0;
 static const char descriptor[]="{\"schema\":\"riscrte.bootstrap\",\"schema_version\":2,\"profile_key\":\"profile\",\"time_key\":\"time\"}";
 if(!strcmp(key,"descriptor")){p=descriptor;size=sizeof(descriptor)-1;}else if(!strcmp(key,"profile")){p=v.profile;size=v.profileSize;}else if(!strcmp(key,"time")){p=v.time;size=v.timeSize;}else return InputStatus::Missing;
 if(size>cap)return InputStatus::Invalid;
 memcpy(out,p,size);*n=size;return InputStatus::Ready;
 }};}
}
