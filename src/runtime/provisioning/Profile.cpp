#include "Profile.h"
#include "bootstrap/Json.h"
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cstdio>
namespace RiscProvision {
void Profile::clear(){volatile unsigned char* p=reinterpret_cast<volatile unsigned char*>(this);for(size_t i=0;i<sizeof(*this);++i)p[i]=0;}
namespace {
// ArduinoJson copies strings from const input. Wipe its owned allocations on
// shrink/free as well as the explicit output, without ever logging credentials.
class ClearingAllocator final:public ArduinoJson::Allocator {
 struct alignas(std::max_align_t) Header {size_t size;};
 public:
 void* allocate(size_t size) override {
   if(size>SIZE_MAX-sizeof(Header))return nullptr;
   auto* h=static_cast<Header*>(malloc(sizeof(Header)+size));if(!h)return nullptr;
   h->size=size;return h+1;
 }
 void deallocate(void* p) override {
   if(!p)return;
   auto* h=static_cast<Header*>(p)-1;
   volatile unsigned char* bytes=static_cast<volatile unsigned char*>(p);
   for(size_t i=0;i<h->size;++i)bytes[i]=0;
   free(h);
 }
 void* reallocate(void* p,size_t size) override {
   if(!p)return allocate(size);
   if(!size){deallocate(p);return nullptr;}
   void* next=allocate(size);if(!next)return nullptr;
   memcpy(next,p,std::min(size,(static_cast<Header*>(p)-1)->size));deallocate(p);return next;
 }
};
int hex(char c){return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:-1;}
bool source(const char* s){
  // No userinfo, query credentials, fragments, ports or escaped
  // delimiters in schema 1. Native transport retains verified, bounded HTTPS redirects.
  if(strncmp(s,"https://",8))return false;
  const char* host=s+8;const char* slash=strchr(host,'/');
  if(!slash || slash==host || slash[1]==0)return false;
  for(const char* p=host;p<slash;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='-'))return false;
  char path[400];return RiscBoot::path("",slash+1,path,sizeof(path));
}
bool decode(const char* bytes,size_t size,Profile& out){
  ClearingAllocator allocator;JsonDocument doc(&allocator);
  if(!RiscBoot::parse(bytes,size,doc))return false;
  auto root=doc.as<JsonObjectConst>();int64_t n=0;
  if(!RiscBoot::eq(root["schema"],"riscrte.provisioning") ||
     !RiscBoot::integer(root["schema_version"],1,3,n))return false;
  const bool compact=n==2,image=n==3;char base[385]{};
  if(image){
    if(!RiscBoot::keys(root,{"schema","schema_version","wifi","image","files"}))return false;
    auto object=root["image"].as<JsonObjectConst>();char digest[65];
    if(!RiscBoot::keys(object,{"url","bytes","sha256"}) ||
       !RiscBoot::text(object["url"],out.image.url,sizeof(out.image.url)) || !source(out.image.url) ||
       !RiscBoot::integer(object["bytes"],1,MaxFileBytes,n) || n%4096 ||
       !RiscBoot::text(object["sha256"],digest,sizeof(digest)) || strlen(digest)!=64)return false;
    out.image.bytes=uint32_t(n);
    for(unsigned j=0;j<32;++j){int a=hex(digest[2*j]),b=hex(digest[2*j+1]);if(a<0||b<0)return false;out.image.sha256[j]=uint8_t(a*16+b);}
  }
  if(compact){
    if(!RiscBoot::keys(root,{"schema","schema_version","wifi","base_url","files"}) ||
       !RiscBoot::text(root["base_url"],base,sizeof(base)))return false;
    const size_t length=strlen(base);char probe[400];
    // Validate a real leaf under the supplied directory, never normalize a
    // query, escape, dot component or missing trailing delimiter.
    if(!length || base[length-1]!='/' || length+2>sizeof(probe))return false;
    memcpy(probe,base,length);memcpy(probe+length,"x",2);
    if(!source(probe))return false;
  }else if(!image && !RiscBoot::keys(root,{"schema","schema_version","wifi","files"}))return false;
  auto wifi=root["wifi"].as<JsonObjectConst>();
  if(!RiscBoot::keys(wifi,{"ssid","password"}) || !RiscBoot::text(wifi["ssid"],out.ssid,sizeof(out.ssid)) ||
     !wifi["password"].is<const char*>())return false;
  auto password=wifi["password"].as<JsonString>();
  if(password.size()>63 || (password.size()!=0 && password.size()<8) ||
     strlen(password.c_str())!=password.size() || !RiscBoot::utf8(password.c_str(),password.size()))return false;
  memcpy(out.password,password.c_str(),password.size()+1);
  auto files=root["files"].as<JsonArrayConst>();
  if(files.isNull() || files.size()<3 || files.size()>MaxFiles)return false;
  uint32_t total=0;bool boot=false,board=false,app=false;
  for(auto value:files){
    auto object=value.as<JsonObjectConst>();auto& file=out.files[out.count];char joined[200],digest[65];
    if(!((compact||image)?RiscBoot::keys(object,{"path","bytes","sha256"}):RiscBoot::keys(object,{"path","url","bytes","sha256"})) ||
       !RiscBoot::text(object["path"],file.path,sizeof(file.path)) || !RiscBoot::path("",file.path,joined,sizeof(joined)) ||
       !RiscBoot::integer(object["bytes"],1,MaxFileBytes,n) ||
       !RiscBoot::text(object["sha256"],digest,sizeof(digest)) || strlen(digest)!=64)return false;
    if(compact){
      const size_t baseLength=strlen(base),pathLength=strlen(file.path);
      if(baseLength+pathLength>=sizeof(file.url))return false;
      memcpy(file.url,base,baseLength);memcpy(file.url+baseLength,file.path,pathLength+1);
      if(!source(file.url))return false;
    }else if(!image && (!RiscBoot::text(object["url"],file.url,sizeof(file.url)) || !source(file.url)))return false;
    file.bytes=uint32_t(n);if(file.bytes>MaxStoreBytes-total)return false;total+=file.bytes;
    for(unsigned j=0;j<32;++j){int a=hex(digest[2*j]),b=hex(digest[2*j+1]);if(a<0||b<0)return false;file.sha256[j]=uint8_t(a*16+b);}
    for(size_t j=0;j<out.count;++j){
      const char* previous=out.files[j].path;size_t a=strlen(previous),b=strlen(file.path);
      if(!strcmp(previous,file.path) || (a<b && !strncmp(previous,file.path,a) && file.path[a]=='/') ||
         (b<a && !strncmp(previous,file.path,b) && previous[b]=='/'))return false;
    }
    boot|=!strcmp(file.path,"boot.json");board|=!strcmp(file.path,"board.json");app|=!strcmp(file.path,"default.elf");++out.count;
  }
  return boot && board && app;
}
}
bool parseProfile(const char* bytes,size_t size,Profile& out){out.clear();if(decode(bytes,size,out))return true;out.clear();return false;}
}
