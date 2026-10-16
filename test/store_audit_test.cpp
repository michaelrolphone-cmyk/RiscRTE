#include "runtime/update/StoreAudit.h"
#include "runtime/provisioning/Profile.h"
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <iostream>
namespace fs=std::filesystem;
static uint32_t ticks=0;static bool safe=true;
static uint32_t now(void*){return ticks;}static bool checkpoint(void*){++ticks;return safe;}
static void write(const fs::path& p,const std::string& s){std::ofstream(p,std::ios::binary)<<s;}
int main(int argc,char** argv){assert(argc==2);fs::path root=argv[1],a=root/"active",b=root/"staged";fs::create_directories(a);fs::create_directories(b);
 for(auto path:{a,b}){write(path/"app.elf","old");write(path/"app.json","old-manifest");write(path/"board.json","immutable-board");write(path/"boot.json","immutable-grants");write(path/"driver.elf",std::string(8193,'d'));}
 write(b/"app.elf","new-elf");write(b/"app.json","new-manifest");std::array<uint8_t,4096> scratch{};
 auto audit=[&](){return RiscUpdate::auditStore(a.c_str(),b.c_str(),"app.elf","app.json",scratch.data(),scratch.size(),nullptr,now,checkpoint);};
 assert(audit());write(b/"driver.elf",std::string(8192,'d')+"x");assert(!audit());write(b/"driver.elf",std::string(8193,'d'));
 write(b/"board.json","altered-board");assert(!audit());write(b/"board.json","immutable-board");
 write(b/"extra.elf","extra");assert(!audit());fs::remove(b/"extra.elf");
 fs::remove(b/"boot.json");assert(!audit());write(b/"boot.json","immutable-grants");
 fs::remove(b/"app.json");assert(!audit());write(b/"app.json","new-manifest");
 safe=false;assert(!audit());safe=true;assert(audit());
 // Preserve ordinary app-update audits for every supported provisioned
 // payload, including the 132-file Watch cohort and exact fixed upper bound.
 for(size_t count:{size_t(128),size_t(132),RiscProvision::MaxFiles}){
  for(size_t i=5;i<count;++i){write(a/("extra-"+std::to_string(i)),"x");write(b/("extra-"+std::to_string(i)),"x");}
  assert(audit());
  const auto last="extra-"+std::to_string(count-1);
  write(b/last,"y");assert(!audit());write(b/last,"x");assert(audit());
  fs::remove(b/last);assert(!audit());write(b/last,"x");
  write(b/"unexpected","x");assert(!audit());fs::remove(b/"unexpected");
 }
 // At the cap, corruption of any extra file still fails. Enumeration order
 // must never turn a late entry into an unchecked/truncated inventory.
 for(size_t i=5;i<RiscProvision::MaxFiles;++i){const auto name="extra-"+std::to_string(i);
  write(b/name,"y");assert(!audit());write(b/name,"x");}
 assert(audit());
 write(a/"excess","x");assert(!audit());write(b/"excess","x");assert(!audit());
 fs::remove(a/"excess");assert(!audit());fs::remove(b/"excess");assert(audit());
 // The unchanged private digest is the only permitted extra object. Its
 // length/content/presence cannot be changed or passed as an update target.
 const char* identity=".provision-sha256";
 write(a/identity,std::string(32,'a'));assert(!audit());
 write(b/identity,std::string(32,'a'));assert(audit());
 write(b/identity,std::string(32,'b'));assert(!audit());
 write(a/identity,std::string(31,'a'));write(b/identity,std::string(31,'a'));assert(!audit());
 write(a/identity,std::string(33,'a'));write(b/identity,std::string(33,'a'));assert(!audit());
 write(a/identity,std::string(32,'a'));write(b/identity,std::string(32,'a'));assert(audit());
 assert(!RiscUpdate::auditStore(a.c_str(),b.c_str(),identity,"app.json",scratch.data(),scratch.size(),nullptr,now,checkpoint));
 assert(!RiscUpdate::auditStore(a.c_str(),b.c_str(),"app.elf",identity,scratch.data(),scratch.size(),nullptr,now,checkpoint));
 write(a/"excess","x");write(b/"excess","x");assert(!audit());
 std::cout<<"Store audit: 128/132/160 payloads, every late-file corruption, missing/extra/161st payload refusal, immutable optional provisioning digest and existing app-update scope PASS\n";
}
