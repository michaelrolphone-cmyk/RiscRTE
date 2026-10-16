#include "bootstrap/Json.h"
#include "bootstrap/RuntimeFiles.h"
#include "runtime/storage/PosixFiles.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <filesystem>
using namespace RiscBoot;
namespace {
const auto ownerThread=std::this_thread::get_id();
bool owner(){return std::this_thread::get_id()==ownerThread;}
unsigned safetyChecks=0,denyAt=0;
bool safe(){return ++safetyChecks!=denyAt;}
struct Fixture {
 RiscStorage::PosixFiles backing; risc_storage_files_v1 api; RuntimeFiles files;
 unsigned opens=0,reads=0,closes=0;bool failClose=false,failRead=false,reenter=false;
 RuntimeFileView* view=nullptr;
 explicit Fixture(const char*root):backing(root),api(*backing.api()){
  api.context=this;
  api.open=[](void*c,const char*p,uint32_t f,uint32_t*h)->int32_t{
   auto&x=*static_cast<Fixture*>(c);++x.opens;
   if(x.reenter){JsonDocument doc;assert(x.view && !readJson(*x.view,"/active/data.json",doc));}
   return x.backing.api()->open(x.backing.api()->context,p,f,h);
  };
  api.read=[](void*c,uint32_t h,void*b,uint32_t n,uint32_t*a)->int32_t{
   auto&x=*static_cast<Fixture*>(c);++x.reads;if(x.failRead){*a=0;return RISC_FILES_IO;}
   return x.backing.api()->read(x.backing.api()->context,h,b,n,a);
  };
  api.close=[](void*c,uint32_t h)->int32_t{
   auto&x=*static_cast<Fixture*>(c);++x.closes;
   const int32_t rc=x.backing.api()->close(x.backing.api()->context,h);
   // Test releases its OS resource exactly once while modeling uncertain
   // provider completion. Production never retries or clears this custody.
   return x.failClose?RISC_FILES_RETAINED:rc;
  };
  assert(files.bind(&api,owner,safe));
 }
};
struct RetainingAllocator final : ArduinoJson::Allocator {
 RuntimeFiles& files;explicit RetainingAllocator(RuntimeFiles& f):files(f){}
 void* allocate(size_t n)override{files.retainCustody();return std::malloc(n);}
 void deallocate(void*p)override{std::free(p);}
 void* reallocate(void*p,size_t n)override{files.retainCustody();return std::realloc(p,n);}
};
void save(const std::string&p,const char*bytes){std::ofstream f(p);f<<bytes;assert(f.good());}
}
int main(int argc,char**argv){
 assert(argc==2);const std::string active=std::string(argv[1])+"/active",staged=std::string(argv[1])+"/staged";
 std::filesystem::create_directory(active);std::filesystem::create_directory(staged);
 save(active+"/data.json","{\"source\":\"active\"}");save(staged+"/data.json","{\"source\":\"staged\"}");
 save(active+"/bad.json","{");
 Fixture a(active.c_str()),b(staged.c_str());
 RuntimeFileSources::Entry bindings[]={{"/active",&a.files},{"/staged",&b.files}};
 RuntimeFileSources sources(bindings,2);RuntimeFileView av(&sources,"/active"),bv(&sources,"/staged");
 JsonDocument doc;bool retained=false;
 assert(readJson(av,"/active/data.json",doc,&retained) && eq(doc["source"],"active") && !retained);
 assert(readJson(bv,"/staged/data.json",doc) && eq(doc["source"],"staged"));
 const unsigned before=a.opens+b.opens;
 assert(!readJson(av,"/staged/data.json",doc) && !readJson(av,"/active-other/data.json",doc));
 assert(!readJson(RuntimeFileView(nullptr,"/active"),"/active/data.json",doc));
 assert(!readJson(RuntimeFileView(&sources,"/absent"),"/absent/data.json",doc));
 assert(a.opens+b.opens==before);
 std::thread foreign([&]{JsonDocument other;assert(!readJson(av,"/active/data.json",other));});foreign.join();assert(a.opens+b.opens==before);
 a.view=&av;a.reenter=true;assert(readJson(av,"/active/data.json",doc));a.reenter=false;
 assert(!readJson(av,"/active/bad.json",doc) && a.files.exitSafe());
 a.failRead=true;assert(!readJson(av,"/active/data.json",doc) && a.files.exitSafe());a.failRead=false;
 a.failClose=true;assert(!readJson(av,"/active/data.json",doc,&retained) && retained && sources.retained());
 const unsigned closed=a.closes,opened=a.opens+b.opens,reads=a.reads+b.reads;
 retained=false;assert(!readJson(bv,"/staged/data.json",doc,&retained) && retained && !b.files.retained());
 assert(!readJson(av,"/active/data.json",doc) && a.closes==closed && a.opens+b.opens==opened && a.reads+b.reads==reads);
 // Open/read/EOF each perform pre/post custody checks. Deny the next
 // close preflight: the reader must retain its known token, not forget it.
 Fixture blocked(active.c_str());RuntimeFileSources::Entry blockedEntry{"/active",&blocked.files};
 RuntimeFileSources blockedSources(&blockedEntry,1);RuntimeFileView blockedView(&blockedSources,"/active");
 safetyChecks=0;denyAt=7;retained=false;
 assert(!readJson(blockedView,"/active/data.json",doc,&retained) && retained && !blocked.closes);
 denyAt=0;assert(blockedSources.retained() && !blocked.files.exitSafe());
 assert(!readJson(blockedView,"/active/data.json",doc) && blocked.opens==1 && !blocked.closes);
 assert(!blocked.backing.api()->close(blocked.backing.api()->context,1));
 // Parser allocation is also a callback boundary: a successful parse cannot
 // hide a sibling source's terminal custody transition after the clean close.
 Fixture parsed(active.c_str()),sibling(staged.c_str());
 RuntimeFileSources::Entry parseEntries[]={{"/active",&parsed.files},{"/staged",&sibling.files}};
 RuntimeFileSources parseSources(parseEntries,2);RetainingAllocator allocator(sibling.files);JsonDocument parsedDoc(&allocator);
 retained=false;assert(!readJson(RuntimeFileView(&parseSources,"/active"),"/active/data.json",parsedDoc,&retained));
 assert(retained && parsed.closes==1 && !parsed.files.retained() && sibling.files.retained());
 puts("Provider JSON: active/staged authority, absent source, owner/reentrancy, parse/read errors, single-close and shared terminal custody PASS");
}
