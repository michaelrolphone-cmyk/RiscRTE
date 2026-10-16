#include "bootstrap/RuntimeFiles.h"
#include "runtime/storage/PosixFiles.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <thread>
#include <string>
#include <unistd.h>
using RiscBoot::RuntimeFiles;
using RiscBoot::RuntimeFileSources;
static bool safe=true;
static const auto thread=std::this_thread::get_id();
static bool owner(){return std::this_thread::get_id()==thread;}
static bool available(){return safe;}
static RuntimeFiles* client;
static int32_t (*realOpen)(void*,const char*,uint32_t,uint32_t*);
static int32_t (*realClose)(void*,uint32_t);
static unsigned calls=0;
static bool reentered=false;
static int32_t reentrantOpen(void*c,const char*p,uint32_t f,uint32_t*h){
 ++calls;uint32_t token=77;
 assert(client->open("/other",RISC_FILES_READ,&token)==RISC_FILES_BUSY&&!token);
 assert(!client->exitSafe());reentered=true;return realOpen(c,p,f,h);
}
static int32_t uncertainClose(void*c,uint32_t h){++calls;assert(!realClose(c,h));return RISC_FILES_IO;}
static int32_t loseCustody(void*c,const char*p,uint32_t f,uint32_t*h){++calls;const int32_t rc=realOpen(c,p,f,h);safe=false;return rc;}
int main(int argc,char**argv){
 assert(argc==2);const std::string root=argv[1];
 RuntimeFiles absent;uint32_t h=8;assert(absent.open("/a",RISC_FILES_READ,&h)==RISC_FILES_UNAVAILABLE&&!h&&absent.exitSafe());
 RiscStorage::PosixFiles provider(root.c_str());auto table=*provider.api();RuntimeFiles files;client=&files;
 assert(files.bind(&table,owner,available));assert(!files.bind(&table,owner,available));
 assert(files.open("/../x",RISC_FILES_READ,&h)==RISC_FILES_INVALID);
 assert(files.open("/missing",RISC_FILES_READ,&h)==RISC_FILES_NOT_FOUND);
 assert(!files.open("/data",RISC_FILES_READ|RISC_FILES_WRITE|RISC_FILES_CREATE|RISC_FILES_EXCLUSIVE,&h));assert(!files.exitSafe());
 uint32_t n=0;assert(!files.write(h,"abcdef",6,&n)&&n==6);assert(!files.sync(h));
 uint64_t length=0,at=0;assert(!files.info(h,&length,&at)&&length==6&&at==6);
 assert(files.seek(h,7)==RISC_FILES_INVALID);assert(!files.seek(h,0));char bytes[8]{};assert(!files.read(h,bytes,6,&n)&&n==6&&!memcmp(bytes,"abcdef",6));assert(!files.read(h,bytes,2,&n)&&!n);
 assert(!files.truncate(h,4));assert(!files.close(h)&&files.exitSafe());assert(files.close(h)==RISC_FILES_INVALID);
 assert(files.open("/data",RISC_FILES_WRITE|RISC_FILES_CREATE|RISC_FILES_EXCLUSIVE,&h)==RISC_FILES_EXISTS);
 assert(!files.open("/data",RISC_FILES_WRITE|RISC_FILES_APPEND,&h));assert(!files.seek(h,0));assert(!files.write(h,"XY",2,&n)&&n==2);assert(!files.info(h,&length,&at)&&length==6&&at==6);assert(!files.close(h));
 assert(!files.mkdir("/dir"));assert(!files.rename("/data","/dir/data"));assert(files.rename("/dir","/dir/data")==RISC_FILES_EXISTS);
 risc_storage_files_stat_v1 info{sizeof(info)};assert(!files.stat("/dir/data",&info)&&info.size==6);
 assert(!files.dirOpen("/dir",&h));risc_storage_files_entry_v1 entry{sizeof(entry)};assert(!files.dirNext(h,&entry)&&!strcmp(entry.name,"data")&&entry.size==6);assert(files.dirNext(h,&entry)==RISC_FILES_END);assert(!files.dirRewind(h));assert(!files.dirTell(h,&at));assert(!files.dirSeek(h,at));assert(!files.dirClose(h));
 // The optional identity supplement preserves native replacement/change
 // evidence and translates logical tokens into the matching provider domain.
 RiscStorage::PosixFiles identityProvider(root.c_str());RuntimeFiles identityFiles;
 auto identityTable=*identityProvider.identityApi();identityTable.context=nullptr;
 assert(!identityFiles.bind(identityProvider.api(),owner,available,&identityTable));
 assert(identityFiles.bind(identityProvider.api(),owner,available,identityProvider.identityApi()));
 const auto& raw=*identityProvider.api();uint32_t rawToken=0,identified=0;
 assert(!raw.open(raw.context,"/different",RISC_FILES_WRITE|RISC_FILES_CREATE,&rawToken));
 assert(!raw.write(raw.context,rawToken,"z",1,&n));
 assert(!identityFiles.open("/dir/data",RISC_FILES_READ,&identified));
 risc_storage_files_identity_value_v1 byPath{sizeof(byPath)},byHandle{sizeof(byHandle)};
 assert(!identityFiles.identity("/dir/data",&byPath)&&byPath.size==6);
 assert(!identityFiles.identity(identified,&byHandle)&&RiscBoot::sameFileIdentity(byPath,byHandle));
 assert(identityFiles.identity(rawToken+999,&byHandle)==RISC_FILES_INVALID);
 assert(files.identity("/dir/data",&byHandle)==RISC_FILES_UNAVAILABLE);
 assert(!raw.close(raw.context,rawToken));
 assert(!raw.open(raw.context,"/replacement",RISC_FILES_WRITE|RISC_FILES_CREATE,&rawToken));
 assert(!raw.write(raw.context,rawToken,"abcdef",6,&n));assert(!raw.close(raw.context,rawToken));
 assert(!::rename((root+"/replacement").c_str(),(root+"/dir/data").c_str()));
 assert(!identityFiles.identity("/dir/data",&byPath));
 assert(!identityFiles.identity(identified,&byHandle)&&!RiscBoot::sameFileIdentity(byPath,byHandle));
 assert(!identityFiles.close(identified));
 RuntimeFileSources::Entry bindings[]={{"/active",&files},{"/staged",&identityFiles}};
 RuntimeFileSources sources(bindings,2);assert(sources.find("/active")==&files&&sources.find("/staged")==&identityFiles);
 assert(!sources.find("/absent")&&!sources.retained());
 RuntimeFileSources::Entry duplicate[]={{"/active",&files},{"/active",&identityFiles}};
 RuntimeFileSources ambiguous(duplicate,2);assert(!ambiguous.find("/active"));
 std::thread foreign([&]{uint32_t token=9;assert(files.open("/dir/data",RISC_FILES_READ,&token)==RISC_FILES_CONTEXT&&!token);});foreign.join();
 safe=false;assert(files.stat("/dir/data",&info)==RISC_FILES_BUSY);safe=true;
 realOpen=table.open;table.open=reentrantOpen;assert(!files.open("/dir/data",RISC_FILES_READ,&h)&&reentered&&calls==1);assert(!files.close(h));table.open=realOpen;
 // Two Runtime/candidate callers share this explicit binding and its sticky
 // custody barrier; a failed parse/read cannot hide the provider's retention.
 auto& candidate=files;realClose=table.close;table.close=uncertainClose;assert(!candidate.open("/dir/data",RISC_FILES_READ,&h));assert(candidate.close(h)==RISC_FILES_RETAINED);assert(files.retained()&&!files.exitSafe()&&!candidate.exitSafe()&&sources.retained());
 unsigned prior=calls;assert(files.stat("/dir/data",&info)==RISC_FILES_RETAINED&&calls==prior);assert(!files.bind(provider.api(),owner,available));
 // Separate provider for a separate session, never a rebind of retained state.
 RiscStorage::PosixFiles other(root.c_str());auto otherTable=*other.api();RuntimeFiles second;assert(second.bind(&otherTable,owner,available));realOpen=otherTable.open;otherTable.open=loseCustody;uint32_t lost=0;assert(second.open("/dir/data",RISC_FILES_READ,&lost)==RISC_FILES_RETAINED);safe=true;assert(second.retained()&&!second.exitSafe());assert(second.close(lost)==RISC_FILES_RETAINED);
 // Fixture releases its OS descriptor only after validating no client cleanup.
 // Production provider and dependent resources remain retained until restart.
 assert(!otherTable.close(otherTable.context,lost));
 puts("Internal generic files: CRUD, explicit binding, owner/reentrancy, shared retention, absent provider PASS");
}
