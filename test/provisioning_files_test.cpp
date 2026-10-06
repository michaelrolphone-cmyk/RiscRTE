#include "runtime/provisioning/StoreFiles.h"
#include "bootstrap/Runtime.h"
#include <openssl/sha.h>
#include <filesystem>
#include <fstream>
#include <cassert>
#include <vector>
#include <memory>
using namespace RiscProvision;
namespace fs=std::filesystem;
using Bytes=std::vector<uint8_t>;
static bool owner(){return true;}static bool health(risc_runtime_health_v1*){return true;}static void delay(uint32_t){}static bool log(const char*){return true;}
struct Context {
 uint32_t clock=1;bool safe=true,admitted=true;unsigned admissions=0;Bytes hashing;
 FileBackend backend(){return {this,[](void* c){return static_cast<Context*>(c)->clock;},[](void* c){return static_cast<Context*>(c)->safe;},
 [](void* c){static_cast<Context*>(c)->hashing.clear();return true;},
 [](void* c,const void* p,uint32_t n){auto& v=static_cast<Context*>(c)->hashing;auto* b=static_cast<const uint8_t*>(p);v.insert(v.end(),b,b+n);return true;},
 [](void* c,uint8_t* d){auto& v=static_cast<Context*>(c)->hashing;SHA256(v.data(),v.size(),d);return true;},
 [](void* c,const char* root,const Profile&){auto& self=*static_cast<Context*>(c);++self.admissions;if(!self.admitted)return false;
  auto runtime=std::make_unique<RiscBoot::Runtime>(RiscBoot::Port{owner,health,delay,log});return runtime->prepare(root);}};}
};
static Bytes read(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void write(const fs::path& p,const Bytes& data){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),data.size());assert(f.good());}
static Bytes bytes(const char* s){return Bytes(s,s+strlen(s));}
int main(int argc,char** argv){assert(argc==2);fs::path base=argv[1];auto p=std::make_unique<Profile>();p->count=3;
 const char* names[]={"boot.json","board.json","default.elf"};
 Bytes files[]={bytes(R"({"board":"board.json","default_app":"default.elf","drivers":[]})"),bytes(R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})"),read(base/"default.elf")};
 uint8_t digest[32]{};digest[0]=9;uint32_t total=32;
 for(unsigned i=0;i<3;++i){strcpy(p->files[i].path,names[i]);p->files[i].bytes=files[i].size();SHA256(files[i].data(),files[i].size(),p->files[i].sha256);total+=files[i].size();}
 auto feed=[&](StoreFiles& s){for(size_t i=0;i<3;++i)for(size_t at=0;at<files[i].size();){uint32_t n=std::min<size_t>(4096,files[i].size()-at);assert(s.write(i,files[i].data()+at,n));at+=n;}};
 for(unsigned mode=0;mode<6;++mode){auto root=base/("files-"+std::to_string(mode));fs::create_directory(root);write(root/"old.json",bytes("old"));Context ctx;StoreFiles s(ctx.backend());
  assert(s.begin(root.c_str(),*p,digest,total));assert(!fs::exists(root/"old.json"));assert(!s.finish());
  assert(!s.write(1,files[1].data(),1));assert(!s.write(0,nullptr,1));feed(s);
  if(mode==0){assert(s.finish());assert(read(root/StoreFiles::DigestFile)==Bytes(digest,digest+32));assert(ctx.admissions==1);assert(!s.finish());assert(!s.write(2,files[2].data(),1));}
  if(mode==1){write(root/"board.json",bytes("corrupt"));assert(!s.finish() && !ctx.admissions);}
  if(mode==2){write(root/"unexpected",bytes("extra"));assert(!s.finish() && !ctx.admissions);}
  if(mode==3){ctx.admitted=false;assert(!s.finish() && ctx.admissions==1);assert(!fs::exists(root/StoreFiles::DigestFile));}
  if(mode==4){ctx.clock=300001;assert(!s.finish() && !ctx.admissions);}
  if(mode==5){ctx.safe=false;assert(!s.finish() && !ctx.admissions);}
  assert(s.close());
 }
 {auto root=base/"capacity";fs::create_directory(root);write(root/"sentinel",bytes("preserve"));Context ctx;StoreFiles s(ctx.backend());assert(!s.begin(root.c_str(),*p,digest,total-1));assert(read(root/"sentinel")==bytes("preserve"));}
 {auto root=base/"hash";fs::create_directory(root);Context ctx;StoreFiles s(ctx.backend());assert(s.begin(root.c_str(),*p,digest,total));auto bad=files[0];bad[0]^=1;assert(!s.write(0,bad.data(),bad.size()));assert(!s.finish());assert(s.close());}
 {auto root=base/"symlink";fs::create_directory(root);write(base/"outside",bytes("unchanged"));fs::create_symlink(base/"outside",root/"link");Context ctx;StoreFiles s(ctx.backend());assert(!s.begin(root.c_str(),*p,digest,total));assert(read(base/"outside")==bytes("unchanged"));}
 {auto root=base/"interrupted";fs::create_directory(root);Context ctx;{StoreFiles first(ctx.backend());assert(first.begin(root.c_str(),*p,digest,total));assert(first.write(0,files[0].data(),1));assert(first.close());}StoreFiles retry(ctx.backend());assert(retry.begin(root.c_str(),*p,digest,total));feed(retry);assert(retry.finish());assert(retry.close());}
 // Full bounded inventories (including Watch-sized stores), interrupted
 // retry, final-file corruption and admission refusal never publish identity.
 for(size_t count:{size_t(83),MaxFiles})for(unsigned mode=0;mode<4;++mode){
  auto many=std::make_unique<Profile>();many->count=count;std::vector<Bytes> contents(files,files+3);uint32_t capacity=total;
  for(size_t i=0;i<3;++i)many->files[i]=p->files[i];
  for(size_t i=3;i<count;++i){auto& f=many->files[i];snprintf(f.path,sizeof(f.path),"app%zu.json",i);contents.push_back(bytes("{}"));f.bytes=2;SHA256(contents.back().data(),2,f.sha256);capacity+=2;}
  auto root=base/("full-"+std::to_string(count)+"-"+std::to_string(mode));fs::create_directory(root);Context ctx;
  auto feedMany=[&](StoreFiles& s,size_t from,size_t to){for(size_t i=from;i<to;++i)for(size_t at=0;at<contents[i].size();){uint32_t n=std::min<size_t>(4096,contents[i].size()-at);assert(s.write(i,contents[i].data()+at,n));at+=n;}};
  if(mode==1){StoreFiles interrupted(ctx.backend());assert(interrupted.begin(root.c_str(),*many,digest,capacity));feedMany(interrupted,0,count-1);assert(interrupted.close());}
  StoreFiles stage(ctx.backend());assert(stage.begin(root.c_str(),*many,digest,capacity));feedMany(stage,0,count);
  if(mode==2)write(root/many->files[count-1].path,bytes("bad"));
  if(mode==3)ctx.admitted=false;
  assert(stage.finish()==(mode<2));assert(stage.close());
  assert(fs::exists(root/StoreFiles::DigestFile)==(mode<2));
 }
 puts("Production inactive-store files: bounded writes, streamed/readback hashes, exact inventory, real graph admission, metadata readback, capacity, symlink refusal and interrupted retry PASS");
}
