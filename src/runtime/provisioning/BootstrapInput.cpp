#include "BootstrapInput.h"
#include "bootstrap/Json.h"
#include <cstring>
namespace RiscProvision {
namespace {
struct Wipe {void* bytes;uint32_t size;~Wipe(){auto* p=static_cast<volatile uint8_t*>(bytes);if(p)while(size--) *p++=0;}};
bool key(const char* value){
 if(!value||!*value||strlen(value)>15)return false;
 for(const char* p=value;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='.'||*p=='-'))return false;
 return true;
}
}
InputStatus loadDescriptor(Input source,Descriptor& out){
 out={};if(!source.read)return InputStatus::Unavailable;
 uint8_t descriptor[DescriptorBytes]{};Wipe descriptorWipe{descriptor,sizeof(descriptor)};uint32_t size=0;
 auto status=source.read(source.context,"descriptor",descriptor,sizeof(descriptor),&size);
 if(status!=InputStatus::Ready)return status;
 if(!size||size>sizeof(descriptor))return InputStatus::Invalid;
 JsonDocument json;int64_t version=0;
 if(!RiscBoot::parse(reinterpret_cast<const char*>(descriptor),size,json))return InputStatus::Invalid;
 auto root=json.as<JsonObjectConst>();
 if(!RiscBoot::keys(root,{"schema","schema_version","profile_key"},{"time_key"})||!RiscBoot::eq(root["schema"],"riscrte.bootstrap")||
    !RiscBoot::integer(root["schema_version"],1,2,version)||!RiscBoot::text(root["profile_key"],out.profileKey,sizeof(out.profileKey))||!key(out.profileKey))return InputStatus::Invalid;
 if(version==1){if(root.size()!=3)return InputStatus::Invalid;strcpy(out.timeKey,"time");}
 else {
   if(!root["time_key"].is<const char*>())return InputStatus::Invalid;
   const char* t=root["time_key"];if(root["time_key"].as<JsonString>().size()!=strlen(t))return InputStatus::Invalid;
   if(strlen(t)>=sizeof(out.timeKey)||(*t&&!key(t)))return InputStatus::Invalid;
   strcpy(out.timeKey,t);
 }
 out.version=unsigned(version);return InputStatus::Ready;
}
InputStatus loadProfile(Input source,void* scratch,uint32_t capacity,Profile& profile,uint8_t (&digest)[32],bool (*hash)(const void*,uint32_t,uint8_t*)){
 profile.clear();memset(digest,0,32);Wipe wipe{scratch,capacity};
 if(!source.read||!scratch||capacity<ProfileInputBytes||!hash)return InputStatus::Unavailable;
 Descriptor descriptor;auto status=loadDescriptor(source,descriptor);
 if(status!=InputStatus::Ready)return status;
 uint32_t size=0;status=source.read(source.context,descriptor.profileKey,scratch,ProfileInputBytes,&size);
 if(status!=InputStatus::Ready)return status;
 if(!size||size>ProfileInputBytes||!parseProfile(static_cast<const char*>(scratch),size,profile)||!hash(scratch,size,digest)){
   profile.clear();memset(digest,0,32);return InputStatus::Invalid;
 }
 return InputStatus::Ready;
}
}
