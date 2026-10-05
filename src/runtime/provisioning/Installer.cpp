#include "Installer.h"
#include "TimeInput.h"
#include "bootstrap/Json.h"
#include <cstring>
#include <cstdio>
#include <memory>
namespace RiscProvision {
namespace {
constexpr char owner[]="riscrte-installer-v1";
constexpr const char* slots[]={"install_p0","install_p1","install_t0","install_t1"};
struct Clear {void* p;uint32_t n;~Clear(){auto* b=static_cast<volatile unsigned char*>(p);if(b)while(n--)*b++=0;}};
bool timeValid(const void* bytes,uint32_t size){
 if(!size)return true;
 if(!bytes||size>384)return false;
 JsonDocument json;if(!RiscBoot::parse(static_cast<const char*>(bytes),size,json))return false;
 auto root=json.as<JsonObjectConst>();int64_t version;
 if(!RiscBoot::keys(root,{"schema","schema_version","servers"})||!RiscBoot::eq(root["schema"],"riscrte.sntp")||!RiscBoot::integer(root["schema_version"],1,1,version))return false;
 auto servers=root["servers"].as<JsonArrayConst>();if(servers.isNull()||servers.size()<1||servers.size()>3)return false;
 for(auto item:servers){char name[64];if(!RiscBoot::text(item,name,sizeof(name))||!timeServer(name))return false;}return true;
}
bool same(InstallTransport t,const char* key,const void* expected,uint32_t size,void* scratch,uint32_t cap){
 uint32_t n=0;return t.input.read(t.input.context,key,scratch,cap,&n)==InputStatus::Ready&&n==size&&!memcmp(scratch,expected,n);
}
bool write(InstallTransport t,const char* key,const void* bytes,uint32_t size,void* scratch,uint32_t cap){
 return t.set(t.input.context,key,bytes,size)&&t.commit(t.input.context)&&same(t,key,bytes,size,scratch,cap);
}
}
bool validInstallInput(const void* bytes,uint32_t size,const void* time,uint32_t timeSize){
 auto profile=std::unique_ptr<Profile>(new(std::nothrow) Profile);
 return profile&&bytes&&size&&size<=ProfileInputBytes&&parseProfile(static_cast<const char*>(bytes),size,*profile)&&timeValid(time,timeSize);
}
InstallResult install(InstallTransport t,const void* bytes,uint32_t size,const void* time,uint32_t timeSize,void* scratch,uint32_t capacity){
 Clear wipe{scratch,capacity};
 if(!t.input.read||!t.set||!t.commit||!scratch||capacity<ProfileInputBytes)return InstallResult::Unavailable;
 auto profile=std::unique_ptr<Profile>(new(std::nothrow) Profile);
 if(!profile)return InstallResult::Unavailable;
 if(!bytes||!size||size>ProfileInputBytes||!parseProfile(static_cast<const char*>(bytes),size,*profile)||!timeValid(time,timeSize))return InstallResult::InvalidInput;
 Descriptor current;auto status=loadDescriptor(t.input,current);
 if(status!=InputStatus::Ready&&status!=InputStatus::Missing)return InstallResult::InvalidExisting;
 if(status==InputStatus::Ready){
   uint32_t n=0;auto old=std::unique_ptr<Profile>(new(std::nothrow) Profile);
   if(!old)return InstallResult::Unavailable;
   if(t.input.read(t.input.context,current.profileKey,scratch,capacity,&n)!=InputStatus::Ready||!n||n>capacity||!parseProfile(static_cast<const char*>(scratch),n,*old))return InstallResult::InvalidExisting;
   const bool sameProfile=n==size&&!memcmp(bytes,scratch,size);
   if(sameProfile&&((!timeSize&&!current.timeKey[0])||(timeSize&&same(t,current.timeKey,time,timeSize,scratch,capacity))))return InstallResult::Unchanged;
 }
 uint32_t n=0;auto owned=t.input.read(t.input.context,"installer",scratch,capacity,&n);
 if(owned==InputStatus::Missing){
   // Adopt only absent reserved keys. Unrelated existing owner blobs stay intact.
   for(const char* key:slots)if(t.input.read(t.input.context,key,scratch,capacity,&n)!=InputStatus::Missing)return InstallResult::InvalidExisting;
   if(!write(t,"installer",owner,sizeof(owner)-1,scratch,capacity))return InstallResult::StageFailed;
 }else if(owned!=InputStatus::Ready||n!=sizeof(owner)-1||memcmp(scratch,owner,n))return InstallResult::InvalidExisting;
 if(status==InputStatus::Ready){
   for(unsigned i=0;i<4;++i){
     if(!strcmp(current.profileKey,slots[i])&&(i>=2||current.version!=2||(*current.timeKey&&strcmp(current.timeKey,slots[i+2]))))return InstallResult::InvalidExisting;
     if(!strcmp(current.timeKey,slots[i])&&(i<2||strcmp(current.profileKey,slots[i-2])))return InstallResult::InvalidExisting;
   }
 }
 unsigned slot=status==InputStatus::Ready&&!strcmp(current.profileKey,slots[0])?1:0;
 // Current descriptor must never name the staging time slot independently.
 if(status==InputStatus::Ready&&!strcmp(current.timeKey,slots[slot+2]))return InstallResult::InvalidExisting;
 if(!write(t,slots[slot],bytes,size,scratch,capacity))return InstallResult::StageFailed;
 if(timeSize&&!write(t,slots[slot+2],time,timeSize,scratch,capacity))return InstallResult::StageFailed;
 char next[DescriptorBytes];int length=snprintf(next,sizeof(next),"{\"schema\":\"riscrte.bootstrap\",\"schema_version\":2,\"profile_key\":\"%s\",\"time_key\":\"%s\"}",slots[slot],timeSize?slots[slot+2]:"");
 if(length<=0||unsigned(length)>=sizeof(next))return InstallResult::StageFailed;
 // Selector is last. Any write/commit/readback error here is uncertain: reload,
 // never erase or blindly repeat. Both old and new complete slots are retained.
 if(!write(t,"descriptor",next,uint32_t(length),scratch,capacity))return InstallResult::SelectionUnknown;
 return InstallResult::Installed;
}
}
