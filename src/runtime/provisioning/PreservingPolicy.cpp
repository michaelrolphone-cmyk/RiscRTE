#include "PreservingPolicy.h"
#include "Profile.h"
#include "bootstrap/Json.h"
#include "runtime/RuntimeLimits.h"
#include "runtime/packages/PackageIdentity.h"
#include "runtime/update/Cohort.h"
#include <cstring>
namespace RiscProvision {
namespace {
// Compare types as well as values; ArduinoJson's ordinary equality can coerce
// numbers. Object ordering/whitespace are immaterial, array order is policy.
// Policy schemas use integer numbers only. Refuse floating-point metadata
// rather than letting lossy parsing equate distinct authority values.
bool same(JsonVariantConst a,JsonVariantConst b){
  if(a.isUnbound() || b.isUnbound())return false;
  if(a.is<JsonObjectConst>() || b.is<JsonObjectConst>()){
    if(!a.is<JsonObjectConst>() || !b.is<JsonObjectConst>() || a.size()!=b.size())return false;
    for(JsonPairConst item:a.as<JsonObjectConst>())
      if(!same(item.value(),b[item.key().c_str()]))return false;
    return true;
  }
  if(a.is<JsonArrayConst>() || b.is<JsonArrayConst>()){
    if(!a.is<JsonArrayConst>() || !b.is<JsonArrayConst>() || a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)if(!same(a[i],b[i]))return false;
    return true;
  }
  if(a.is<const char*>() || b.is<const char*>()){
    if(!a.is<const char*>() || !b.is<const char*>())return false;
    JsonString left=a.as<JsonString>(),right=b.as<JsonString>();
    return left.size()==right.size() && !memcmp(left.c_str(),right.c_str(),left.size());
  }
  if(a.is<bool>() || b.is<bool>())return a.is<bool>() && b.is<bool>() && a.as<bool>()==b.as<bool>();
  if(a.is<int64_t>() || b.is<int64_t>())
    return a.is<int64_t>() && b.is<int64_t>() && a.as<int64_t>()==b.as<int64_t>();
  if(a.is<uint64_t>() || b.is<uint64_t>())
    return a.is<uint64_t>() && b.is<uint64_t>() && a.as<uint64_t>()==b.as<uint64_t>();
  return a.isNull() && b.isNull();
}
bool sourceSame(JsonVariantConst a,JsonVariantConst b){
  if(!a.is<JsonObjectConst>() || !b.is<JsonObjectConst>() || a.size()!=b.size())return false;
  if(!a["path"].is<const char*>() || !b["path"].is<const char*>())return false;
  for(JsonPairConst item:a.as<JsonObjectConst>())
    if(strcmp(item.key().c_str(),"path") && !same(item.value(),b[item.key().c_str()]))return false;
  return true;
}
bool selectionsSame(JsonVariantConst a,JsonVariantConst b){
  if(!a.is<JsonArrayConst>() || !b.is<JsonArrayConst>() || a.size()!=b.size())return false;
  for(size_t i=0;i<a.size();++i){
    if(!a[i].is<JsonObjectConst>() || !b[i].is<JsonObjectConst>() || a[i].size()!=b[i].size())return false;
    for(JsonPairConst item:a[i].as<JsonObjectConst>()){
      const char* key=item.key().c_str();
      if(!strcmp(key,"image_source")){
        if(!sourceSame(item.value(),b[i][key]))return false;
      }else if(!same(item.value(),b[i][key]))return false;
    }
  }
  return true;
}
bool bootSame(JsonObjectConst a,JsonObjectConst b){
  if(a.isNull() || b.isNull() || a.size()!=b.size())return false;
  for(JsonPairConst item:a){
    const char* key=item.key().c_str();
    if(!strcmp(key,"drivers") || !strcmp(key,"app_capabilities")){
      if(!selectionsSame(item.value(),b[key]))return false;
    }else if(!same(item.value(),b[key]))return false;
  }
  return true;
}
bool contentPath(JsonVariantConst value,const char* id,const char* directory,uint8_t* digest=nullptr){
  char relative[193],prefix[96];
  if(!RiscBoot::text(value,relative,sizeof(relative)) || !RuntimePackages::safeId(id))return false;
  const int length=snprintf(prefix,sizeof(prefix),"%s/%s/image-",directory,id);
  if(length<0 || static_cast<size_t>(length)>=sizeof(prefix))return false;
  const size_t offset=static_cast<size_t>(length);
  if(strlen(relative)!=offset+64+4 || strncmp(relative,prefix,offset) || strcmp(relative+offset+64,".elf"))return false;
  for(size_t i=0;i<64;++i){
    const char digit=relative[offset+i];
    if(!((digit>='0' && digit<='9') || (digit>='a' && digit<='f')))return false;
    if(digest){
      const uint8_t nibble=uint8_t(digit<='9'?digit-'0':digit-'a'+10);
      if(i%2)digest[i/2]|=nibble;else digest[i/2]=uint8_t(nibble<<4);
    }
  }
  return true;
}
bool manifestSame(JsonObjectConst a,JsonObjectConst b,const char* type){
  char oldVersion[32],newVersion[32];uint32_t oldParts[3],newParts[3];
  if(!RiscBoot::eq(a["type"],type) || !RiscBoot::eq(b["type"],type) ||
     !RiscBoot::text(a["version"],oldVersion,sizeof(oldVersion)) ||
     !RiscBoot::text(b["version"],newVersion,sizeof(newVersion)) ||
     !RiscUpdate::parseVersion(oldVersion,oldParts) || !RiscUpdate::parseVersion(newVersion,newParts))return false;
  const int comparison=RiscUpdate::compareVersion(newParts,oldParts);
  if(comparison<=0)return comparison==0 && same(a,b);
  for(JsonPairConst item:a)
    if(strcmp(item.key().c_str(),"version") && !same(item.value(),b[item.key().c_str()]))return false;
  for(JsonPairConst item:b)
    if(strcmp(item.key().c_str(),"version") && !same(item.value(),a[item.key().c_str()]))return false;
  return true;
}
bool read(const char* root,const char* relative,JsonDocument& doc,bool& retained){
  char filename[256];
  return !retained && RiscBoot::path(root,relative,filename,sizeof(filename)) &&
         RiscBoot::readJson(filename,doc,&retained) && !retained;
}
bool sameFile(const char* installed,const char* candidate,const char* relative,bool& retained){
  JsonDocument a,b;
  return read(installed,relative,a,retained) && read(candidate,relative,b,retained) &&
         same(a.as<JsonVariantConst>(),b.as<JsonVariantConst>());
}
bool manifests(const char* installed,const char* candidate,JsonVariantConst selections,JsonVariantConst newSelections,
               const char* type,size_t maximum,bool& retained){
  if(!selections.is<JsonArrayConst>() || selections.size()>maximum)return false;
  for(size_t i=0;i<selections.size();++i){
    JsonVariantConst item=selections[i];
    char relative[193];JsonDocument a,b;
    if(!item.is<JsonObjectConst>() || !RiscBoot::text(item["manifest"],relative,sizeof(relative)) ||
       !read(installed,relative,a,retained) || !read(candidate,relative,b,retained) ||
       !manifestSame(a.as<JsonObjectConst>(),b.as<JsonObjectConst>(),type))return false;
    const auto oldPath=item["image_source"]["path"],newPath=newSelections[i]["image_source"]["path"];
    if(!oldPath.isUnbound() && !same(oldPath,newPath)){
      char id[64];const char* directory=!strcmp(type,"application")?"Apps":"Drivers";
      const auto oldFallback=item["image_source"]["internal_fallback"],newFallback=newSelections[i]["image_source"]["internal_fallback"];
      if(!oldFallback.is<bool>() || !oldFallback.as<bool>() || !newFallback.is<bool>() || !newFallback.as<bool>() ||
         !RiscBoot::text(a["id"],id,sizeof(id)) || !contentPath(oldPath,id,directory) ||
         !contentPath(newPath,id,directory))return false;
    }
  }
  return true;
}
bool imageSources(const char* root,JsonVariantConst selections,const char* type,size_t maximum,
                  const Profile& profile,bool& retained){
  if(!selections.is<JsonArrayConst>() || selections.size()>maximum)return false;
  for(JsonVariantConst item:selections.as<JsonArrayConst>()){
    if(!item.is<JsonObjectConst>())return false;
    const auto source=item["image_source"];
    if(source.isUnbound())continue;
    if(!source.is<JsonObjectConst>())return false;
    const auto fallback=source["internal_fallback"];
    if(fallback.isUnbound())continue;
    if(!fallback.is<bool>())return false;
    if(!fallback.as<bool>())continue;
    char relative[193],id[64],filename[128],image[193];JsonDocument manifest;uint8_t digest[32];
    if(!RiscBoot::text(item["manifest"],relative,sizeof(relative)) || !read(root,relative,manifest,retained) ||
       !RiscBoot::eq(manifest["type"],type) || !RiscBoot::text(manifest["id"],id,sizeof(id)) ||
       !RiscBoot::text(manifest["file_name"],filename,sizeof(filename)) || !RuntimePackages::safeArtifact(filename) ||
       !contentPath(source["path"],id,!strcmp(type,"application")?"Apps":"Drivers",digest))return false;
    const char* slash=strrchr(relative,'/');const size_t prefix=slash?static_cast<size_t>(slash+1-relative):0;
    const size_t nameLength=strlen(filename);
    if(prefix+nameLength>=sizeof(image))return false;
    memcpy(image,relative,prefix);memcpy(image+prefix,filename,nameLength+1);
    const File* matched=nullptr;
    for(size_t i=0;i<profile.count;++i){
      const File& file=profile.files[i];
      if(!memchr(file.path,0,sizeof(file.path)))return false;
      if(strcmp(file.path,image))continue;
      if(matched || !file.bytes || file.bytes>MaxFileBytes || memcmp(file.sha256,digest,sizeof(digest)))return false;
      matched=&file;
    }
    if(!matched)return false;
  }
  return true;
}
bool cohortSame(const char* installed,const char* candidate,bool& retained){
  RiscUpdate::CohortIdentity a,b;
  if(!RiscUpdate::readCohort(installed,a,&retained) || retained ||
     !RiscUpdate::readCohort(candidate,b,&retained) || retained)return false;
  uint32_t oldVersion[3],newVersion[3];
  if(strcmp(a.product.product,b.product.product) || strcmp(a.product.source_repo,b.product.source_repo) ||
     strcmp(a.runtimeVersion,b.runtimeVersion) || strcmp(a.layout,b.layout) || a.storeAbi!=b.storeAbi ||
     a.firmwareSize!=b.firmwareSize || memcmp(a.firmwareSha,b.firmwareSha,sizeof(a.firmwareSha)) ||
     !RiscUpdate::parseVersion(a.product.version,oldVersion) ||
     !RiscUpdate::parseVersion(b.product.version,newVersion))return false;
  const int comparison=RiscUpdate::compareVersion(newVersion,oldVersion);
  return comparison>0 || (comparison==0 && !strcmp(a.product.source_revision,b.product.source_revision));
}
}
bool preservingPolicy(const char* installed,const char* candidate,bool& retained){
  if(retained || !installed || !candidate || installed[0]!='/' || candidate[0]!='/' ||
     strlen(installed)>=256 || strlen(candidate)>=256)return false;
  JsonDocument oldBoot,newBoot;
  if(!read(installed,"boot.json",oldBoot,retained) || !read(candidate,"boot.json",newBoot,retained) ||
     !bootSame(oldBoot.as<JsonObjectConst>(),newBoot.as<JsonObjectConst>()))return false;
  JsonObjectConst boot=oldBoot.as<JsonObjectConst>();char board[193];
  if(!RiscBoot::text(boot["board"],board,sizeof(board)) || !sameFile(installed,candidate,board,retained) ||
     !manifests(installed,candidate,boot["drivers"],newBoot["drivers"],"driver",RiscLimits::Providers,retained))return false;
  if(!boot["app_capabilities"].isUnbound() &&
     !manifests(installed,candidate,boot["app_capabilities"],newBoot["app_capabilities"],"application",RiscLimits::Apps,retained))return false;
  oldBoot.clear();newBoot.clear();
  return cohortSame(installed,candidate,retained) && !retained;
}
bool preservingImageSources(const char* root,const Profile& profile,bool& retained){
  if(retained || !root || root[0]!='/' || strlen(root)>=256 || !profile.count || profile.count>MaxFiles)return false;
  JsonDocument boot;
  if(!read(root,"boot.json",boot,retained) ||
     !imageSources(root,boot["drivers"],"driver",RiscLimits::Providers,profile,retained))return false;
  return (boot["app_capabilities"].isUnbound() ||
          imageSources(root,boot["app_capabilities"],"application",RiscLimits::Apps,profile,retained)) && !retained;
}
}
