#include "bootstrap/Runtime.h"
#include <cassert>
#include <fstream>
#include <string>
static risc_random_api_v1 oldRandom;static unsigned randomReads;
extern "C" void random_test_keep(const risc_random_api_v1* a){oldRandom=*a;}
static int32_t randomFill(void* p,uint32_t n){++randomReads;for(uint32_t i=0;i<n;++i)static_cast<unsigned char*>(p)[i]=i^0xA5;return 0;}
static bool owned=true;static unsigned reads;static risc_memory_heap_api_v1 old;
extern "C" void memory_test_owner(int v){owned=v;}
extern "C" void memory_test_keep(const risc_memory_heap_api_v1* a){old=*a;}
static bool owner(){return owned;}static bool health(risc_runtime_health_v1*){return true;}
static bool log(const char*){return true;}static void delay(uint32_t){}
static int32_t snapshot(risc_memory_heap_snapshot_v1* s){++reads;if(s->memory_class==RISC_MEMORY_EXTERNAL)return RISC_MEMORY_UNSUPPORTED;s->total_bytes=10000;s->free_bytes=7000;s->largest_block=4096;return 0;}
int main(int argc,char** argv){assert(argc==2);std::string root=argv[1];
 auto file=[&](const char* n,const char* s){std::ofstream(root+"/"+n)<<s;};
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 file("app.json",R"({"type":"application","id":"memory-test","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"memory.heap","api":1},{"capability":"random.bytes","api":1}]})");
 file("boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"memory.heap","api":1,"instance_id":0},{"capability":"random.bytes","api":1,"instance_id":0}]}]})");
 RiscBoot::Port port{owner,health,delay,log};
 {RiscBoot::Runtime unavailable(port);assert(!unavailable.prepare(root.c_str()));assert(!reads);}
 port.memoryHeap=snapshot;
 {RiscBoot::Runtime unavailable(port);assert(!unavailable.prepare(root.c_str()));assert(!randomReads);}
 port.randomBytes=randomFill;
 {RiscBoot::Runtime r(port);assert(r.prepare(root.c_str()));assert(r.run());}
 risc_memory_heap_snapshot_v1 s{sizeof(s)};assert(old.snapshot(old.context,&s)==RISC_MEMORY_CONTEXT);assert(reads==2);
 unsigned char b=0;assert(oldRandom.fill(oldRandom.context,&b,1)==RISC_RANDOM_CONTEXT);assert(randomReads==1);
 puts("Shared random.bytes and memory.heap admission, snapshots, unsupported classes, ownership and grant revocation PASS");
}
