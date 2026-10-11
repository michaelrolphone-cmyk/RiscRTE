#include "bootstrap/Runtime.h"
#include <cassert>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <cstring>
using namespace RiscBoot;
static bool owned=true,safe=true,backendBusy=false;static unsigned calls=0,writesCount=0,gets=0;
static risc_bound_key_value_v2 bound{},staleBound{};static risc_key_value_v2 staleApp{};
static std::map<std::pair<uint32_t,std::string>,std::vector<uint8_t>> values;
static int32_t get(void*,uint32_t ns,const char* key,void* data,uint32_t cap,uint32_t* size){
 if(backendBusy)return RISC_KEY_VALUE_BUSY;
 ++calls;++gets;*size=0;assert(cap==64||cap==2048);
 if(!strcmp(key,"partial")){memset(data,0,cap);*size=17;return -5;}
 if(!strcmp(key,"oversize")){*size=cap+1;return 0;}
 auto it=values.find({ns,key});if(it==values.end())return -1;*size=it->second.size();
 if(*size>cap)return -2;
 memcpy(data,it->second.data(),*size);return 0;
}
static int32_t put(void*,uint32_t ns,const char* key,const void* data,uint32_t n){
 if(backendBusy)return RISC_KEY_VALUE_BUSY;
 ++calls;++writesCount;assert(n&&n<=2048);auto p=static_cast<const uint8_t*>(data);values[{ns,key}]={p,p+n};return !strcmp(key,"uncertain")?-5:0;
}
static KeyValueBackend backend{nullptr,get,put,2048};
static Port port(){return {[](){return owned;},[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},nullptr,&backend,[](){return safe;}};}
extern "C" void test_kv2_provider(const risc_bound_key_value_v2* kv){bound=*kv;}
static void denied(const risc_key_value_v1& kv){
 uint8_t out=0xa5;uint32_t n=99;unsigned before=calls;
 assert(kv.get(kv.context,"value",&out,1,&n)==-4&&out==0xa5&&!n);
 assert(kv.put(kv.context,"value",&out,1)==-4&&calls==before);
}
extern "C" void test_kv2_app(){
 const auto* rt=risc_runtime_get_api(1);assert(rt);
 risc_runtime_capability_v1 a{sizeof(a)},b{sizeof(b)},bad{sizeof(bad)};
 assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,1,&a));
 assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,2,5,&b));
 assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,2,1,&bad));
 assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,5,&bad));
 const auto* small=static_cast<const risc_key_value_v1*>(a.api);
 const auto* large=static_cast<const risc_key_value_v2*>(b.api);assert(small->api_version==1&&large->api_version==2&&bound.api_version==2);
 backendBusy=true;uint8_t untouched=0x55;uint32_t busySize=99;const unsigned busyCalls=calls;
 assert(small->get(small->context,"value",&untouched,1,&busySize)==RISC_KEY_VALUE_BUSY && !busySize && untouched==0x55);
 assert(large->get(large->context,"value",&untouched,1,&busySize)==RISC_KEY_VALUE_BUSY && !busySize);
 assert(bound.get(bound.context,"value",&untouched,1,&busySize)==RISC_BOUND_KEY_VALUE_BUSY && !busySize);
 assert(small->put(small->context,"value",&untouched,1)==RISC_KEY_VALUE_BUSY);
 assert(large->put(large->context,"value",&untouched,1)==RISC_KEY_VALUE_BUSY);
 assert(bound.put(bound.context,"value",&untouched,1)==RISC_BOUND_KEY_VALUE_BUSY && calls==busyCalls);backendBusy=false;
 std::vector<uint8_t> bytes(2049),out(2052,0xa5);for(unsigned i=0;i<bytes.size();++i)bytes[i]=uint8_t(i*37);
 for(uint32_t length:{1u,64u,65u,160u,1296u,2048u}){
  for(unsigned which=0;which<2;++which){
   auto context=which?bound.context:large->context;auto write=which?bound.put:large->put;auto read=which?bound.get:large->get;
   assert(write(context,"value",bytes.data(),length)==0);
   uint32_t n=99;out.assign(out.size(),0xa5);assert(read(context,"value",out.data(),length-1,&n)==-2&&n==length);
   for(auto v:out)assert(v==0xa5);
   assert(read(context,"value",nullptr,0,&n)==-2&&n==length);
   assert(read(context,"value",out.data(),length,&n)==0&&n==length&&!memcmp(out.data(),bytes.data(),length));
   for(unsigned i=length;i<out.size();++i)assert(out[i]==0xa5);
  }
 }
 unsigned before=calls;assert(large->put(large->context,"value",bytes.data(),2049)==-3);
 assert(bound.put(bound.context,"value",bytes.data(),2049)==-3);
 assert(small->put(small->context,"value",bytes.data(),65)==-3&&calls==before);
 assert(bound.put(bound.context,"readonly",bytes.data(),64)==-4);
 uint32_t n=99;assert(bound.get(bound.context,"missing_key",out.data(),2048,&n)==-4&&!n&&calls==before);
 // @1 may not observe the larger object, including through a size probe.
 values[{1,"value"}]=std::vector<uint8_t>(65,4);out.assign(out.size(),0xa5);
 assert(small->get(small->context,"value",out.data(),2048,&n)==-5&&!n);for(auto v:out)assert(v==0xa5);
 for(const char* key:{"partial","oversize"}){
  out.assign(out.size(),0xa5);assert(large->get(large->context,key,out.data(),2048,&n)==-5&&!n);
  assert(bound.get(bound.context,key,out.data(),2048,&n)==-5&&!n);for(auto v:out)assert(v==0xa5);
 }
 unsigned writes=writesCount;assert(large->put(large->context,"uncertain",bytes.data(),1296)==-5&&writesCount==writes+1);
 assert(large->get(large->context,"uncertain",out.data(),2048,&n)==0&&n==1296&&!memcmp(bytes.data(),out.data(),n));
 staleApp=*large;staleBound=bound;
 owned=false;denied(staleApp);risc_key_value_v1 provider{bound.api_version,bound.struct_size,bound.context,bound.get,bound.put};denied(provider);owned=true;
 assert(rt->release(&b));denied(staleApp);assert(rt->release(&a));
}
int main(int argc,char** argv){
 assert(argc==2);std::string root=argv[1];auto write=[&](const char* p,const std::string& s){std::ofstream(root+"/"+p)<<s;};
 write("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"kv2-test","revision":"test","buses":[],"devices":[]})");
 const std::string req1=R"({"capability":"storage.key-value","api":1})",req2=R"({"capability":"storage.key-value","api":2})";
 auto stage=[&](unsigned api,const std::string& requirements){
  write("provider.json",std::string(R"({"type":"driver","id":"kv2-provider","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"provider.elf","provides":[{"capability":"test.kv2","api":1}],"requires":[{"capability":"storage.key-value.bound","api":)")+std::to_string(api)+"}]}");
  write("default.json",std::string(R"({"type":"application","id":"kv2-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[)")+requirements+"]}");
  write("boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"provider.json","key_value":[{"key":"value","namespace":4,"access":"read-write"},{"key":"readonly","namespace":4,"access":"read"},{"key":"partial","namespace":4,"access":"read"},{"key":"oversize","namespace":4,"access":"read"}]}],"app_capabilities":[{"manifest":"default.json","grants":[{"capability":"storage.key-value","api":1,"instance_id":1},{"capability":"storage.key-value","api":2,"instance_id":5}]}]})");
 };
 unsigned before=calls;
 for(unsigned limit:{64u,2047u}){backend.maxBlobSize=limit;stage(2,req1+","+req2);Runtime r(port());assert(!r.prepare(root.c_str()));}
 backend.maxBlobSize=2048;stage(3,req1+","+req2);{Runtime r(port());assert(!r.prepare(root.c_str()));}
 stage(2,req1+","+req2+","+req2);{Runtime r(port());assert(!r.prepare(root.c_str()));}
 stage(2,req1);{Runtime r(port());assert(!r.prepare(root.c_str()));}assert(calls==before);
 stage(2,req1+","+req2);{Runtime r(port());if(!r.prepare(root.c_str())){fprintf(stderr,"prepare failed: %s\n",r.error());abort();}assert(r.run());}
 denied(staleApp);risc_key_value_v1 provider{staleBound.api_version,staleBound.struct_size,staleBound.context,staleBound.get,staleBound.put};denied(provider);
 puts("Explicit KV v2 admission, mixed versions, 2048-byte bounds, uncertain writes, read isolation and lifecycle PASS");
}
