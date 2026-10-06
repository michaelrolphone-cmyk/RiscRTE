#pragma once
#include "bootstrap/Json.h"
#include "Version.h"
#include "PairedBank.h"
namespace RiscUpdate {
struct CohortIdentity {
  risc_bank_cohort_status_v1 product{};
  char runtimeVersion[32]{},layout[32]{};
  uint32_t storeAbi=0,firmwareSize=0;
  uint8_t firmwareSha[32]{};
};
inline bool hex(const char* value,size_t length){
  if(!value || strlen(value)!=length)return false;
  for(size_t i=0;i<length;++i)if(!((value[i]>='0' && value[i]<='9') || (value[i]>='a' && value[i]<='f')))return false;
  return true;
}
inline bool repository(const char* value){
  if(!value || !*value || strlen(value)>=128 || strstr(value,".."))return false;
  unsigned slashes=0;
  for(const char* p=value;*p;++p){
    if(*p=='/'){if(!p[1] || p==value || ++slashes!=1)return false;}
    else if(!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || *p=='_' || *p=='-' || *p=='.'))return false;
  }
  return slashes==1;
}
template<size_t N>inline bool terminated(const char(&value)[N]){return memchr(value,0,N)!=nullptr;}
inline bool validCohortRequest(const risc_bank_cohort_v1& c){
  uint32_t version[3];
  return c.struct_size>=sizeof(c) && terminated(c.product) && terminated(c.version) &&
    terminated(c.runtime_version) && terminated(c.source_repo) && terminated(c.source_revision) &&
    RuntimePackages::safeId(c.product) && parseVersion(c.version,version) &&
    parseVersion(c.runtime_version,version) && repository(c.source_repo) && hex(c.source_revision,40) &&
    c.store_abi==StoreAbi && c.store_size==StoreBytes && c.firmware_size>=32 && c.firmware_size<=FirmwareBytes;
}
inline bool readCohort(const char* root,CohortIdentity& out){
  using namespace RiscBoot;
  char filename[256],digest[65];JsonDocument doc;int64_t number;
  out={};out.product.struct_size=sizeof(out.product);
  if(!path(root,"cohort.json",filename,sizeof(filename)) || !readJson(filename,doc))return false;
  JsonObjectConst c=doc.as<JsonObjectConst>();
  if(!keys(c,{"schema","schema_version","product","version","runtime_version","source_repo","source_revision",
             "layout","store_abi","firmware_size","firmware_sha256"}) ||
     !eq(c["schema"],"riscrte.cohort") || !integer(c["schema_version"],1,1,number) ||
     !text(c["product"],out.product.product,sizeof(out.product.product)) || !RuntimePackages::safeId(out.product.product) ||
     !text(c["version"],out.product.version,sizeof(out.product.version)) ||
     !text(c["runtime_version"],out.runtimeVersion,sizeof(out.runtimeVersion)) ||
     !text(c["source_repo"],out.product.source_repo,sizeof(out.product.source_repo)) || !repository(out.product.source_repo) ||
     !text(c["source_revision"],out.product.source_revision,sizeof(out.product.source_revision)) || !hex(out.product.source_revision,40) ||
     !text(c["layout"],out.layout,sizeof(out.layout)) || strcmp(out.layout,Layout) ||
     !integer(c["store_abi"],StoreAbi,StoreAbi,number))return false;
  out.storeAbi=uint32_t(number);
  if(!integer(c["firmware_size"],32,FirmwareBytes,number) ||
     !text(c["firmware_sha256"],digest,sizeof(digest)) || !hex(digest,64))return false;
  out.firmwareSize=uint32_t(number);uint32_t version[3];
  if(!parseVersion(out.product.version,version) || !parseVersion(out.runtimeVersion,version))return false;
  for(unsigned i=0;i<32;++i){auto digit=[](char x){return x<='9'?x-'0':x-'a'+10;};out.firmwareSha[i]=uint8_t(digit(digest[2*i])*16+digit(digest[2*i+1]));}
  return true;
}
inline bool cohortMatches(const CohortIdentity& c,const risc_bank_cohort_v1& request){
  return !strcmp(c.product.product,request.product) && !strcmp(c.product.version,request.version) &&
    !strcmp(c.product.source_repo,request.source_repo) && !strcmp(c.product.source_revision,request.source_revision) &&
    !strcmp(c.runtimeVersion,request.runtime_version) && c.storeAbi==request.store_abi &&
    c.firmwareSize==request.firmware_size && !memcmp(c.firmwareSha,request.firmware_sha256,32);
}
}
