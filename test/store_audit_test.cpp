#include "runtime/update/StoreAudit.h"
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
 for(unsigned i=0;i<124;++i){write(a/("extra-"+std::to_string(i)),"x");write(b/("extra-"+std::to_string(i)),"x");}
 assert(!audit());
 std::cout<<"Store inventory/unchanged bytes, board/grants/drivers preservation, additions/removals and128-entry bounds PASS\n";
}
