#include "runtime/storage/AppDataFiles.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <unistd.h>
using RiscStorage::AppDataFiles;
static uint32_t ticks=0;
static uint32_t now(void*){return ticks;}
static bool cooperate(void*){++ticks;return true;}
int main(int argc,char**argv){
 assert(argc==2);AppDataFiles store({nullptr,now,cooperate,malloc,free});
 uint32_t n=9;uint64_t revision=0;assert(store.stat(1,"timecard.json",&n,&revision)==RISC_APP_DATA_UNAVAILABLE && !n);
 auto put=[&](uint32_t ns,const char*name,const void*bytes,uint32_t size){uint32_t oldSize=0;uint64_t rev=0;int32_t state=store.stat(ns,name,&oldSize,&rev);if(state && state!=RISC_APP_DATA_NOT_FOUND)return state;return store.replace(ns,name,rev,bytes,size);};
 auto get=[&](uint32_t ns,const char*name,void*out,uint32_t cap,uint32_t*size,uint64_t*rev){uint32_t oldSize=0;uint64_t oldRev=0;int32_t state=store.stat(ns,name,&oldSize,&oldRev);if(state){*size=0;*rev=0;return state;}return store.read(ns,name,oldRev,out,cap,size,rev);};
 assert(store.configure(argv[1]));assert(!store.configure(argv[1]));
 assert(store.stat(1,"timecard.json",&n,&revision)==RISC_APP_DATA_NOT_FOUND);
 for(const char*name:{"",".","..","a/b","a\\b","/abs",".pending","x\n","x\177"})assert(!AppDataFiles::validName(name));
 assert(!AppDataFiles::validName(std::string(49,'x').c_str()));
 assert(put(1,"timecard.json","old",3)==0);
 char out[16];memset(out,42,sizeof(out));
 assert(get(1,"timecard.json",nullptr,0,&n,&revision)==RISC_APP_DATA_BUFFER_SMALL && n==3);
 assert(get(1,"timecard.json",out,2,&n,&revision)==RISC_APP_DATA_BUFFER_SMALL && n==3 && out[0]==42);
 assert(get(2,"timecard.json",out,sizeof(out),&n,&revision)==RISC_APP_DATA_NOT_FOUND && !n);
 assert(get(1,"timecard.json",out,sizeof(out),&n,&revision)==0 && n==3 && !memcmp(out,"old",3));
 std::vector<char> full(49151,'v'),large(RISC_APP_DATA_FILE_MAX,'a'),got(RISC_APP_DATA_FILE_MAX);
 assert(put(1,"timecard.json",full.data(),full.size())==0);
 assert(get(1,"timecard.json",got.data(),got.size(),&n,&revision)==0 && n==full.size() && !memcmp(full.data(),got.data(),n));
 assert(put(1,"timecard.json",large.data(),large.size())==0);
 assert(put(1,"two",large.data(),large.size())==0);
 assert(put(1,"three","x",1)==RISC_APP_DATA_NO_SPACE);
 assert(put(1,"timecard.json",nullptr,0)==0);
 assert(get(1,"timecard.json",nullptr,0,&n,&revision)==0 && !n);
 assert(put(1,"three",nullptr,0)==0);
 assert(put(1,"four",nullptr,0)==0);
 assert(put(1,"five",nullptr,0)==RISC_APP_DATA_NO_SPACE);
 assert(put(2,"timecard.json","separate",8)==0);
 std::string stage=std::string(argv[1])+"/n00000002/.pending";{std::ofstream f(stage);f<<"uncommitted";}
 assert(put(2,"timecard.json","new",3)==0 && access(stage.c_str(),F_OK)!=0);
 std::string link=std::string(argv[1])+"/n00000003";
 assert(symlink((std::string(argv[1])+"/n00000002").c_str(),link.c_str())==0);
 assert(store.stat(3,"timecard.json",&n,&revision)==RISC_APP_DATA_IO);
 assert(put(3,"timecard.json","bad",3)==RISC_APP_DATA_IO);
 assert(put(1,"too-big",large.data(),large.size()+1)==RISC_APP_DATA_INVALID);
 assert(store.stat(2,"timecard.json",&n,&revision)==0);uint64_t oldRevision=revision;
 assert(put(2,"timecard.json","next",4)==0);
 assert(store.replace(2,"timecard.json",oldRevision,"stale",5)==RISC_APP_DATA_STALE);
 assert(store.read(2,"timecard.json",oldRevision,out,sizeof(out),&n,&revision)==RISC_APP_DATA_STALE && !n && !revision);
 assert(store.replace(2,"timecard.json",0,"clobber",7)==RISC_APP_DATA_STALE);
 puts("App-data full-file sizes, probes, namespace/path isolation, quotas and stage recovery PASS");
}
