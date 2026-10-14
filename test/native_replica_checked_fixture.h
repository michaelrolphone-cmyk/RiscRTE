#pragma once
// Host AppData/NVS fixture adapted from native_replica_store_test.cpp.
// Real checked store, real AppDataFiles and real NvsReplicaMetadata; only
// ESP-IDF NVS calls and partition-space accounting are deterministic host hooks.
#include "experimental/native_replica/NvsReplicaMetadata.h"
#include "experimental/native_replica/NativeReplicaJob.h"
#include "experimental/native_replica/NativeRebaseCandidate.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <unistd.h>
using C=RiscExperimental::NativeReplicaCore;using S=RiscExperimental::CheckedNativeReplicaStore;
using N=RiscExperimental::NvsReplicaMetadata;using J=RiscExperimental::NativeReplicaJob;using Bytes=std::vector<uint8_t>;
namespace NativeFixture {
struct Handle {std::string name;nvs_open_mode_t mode;};
std::map<std::string,std::map<std::string,Bytes>> nvs;
std::map<nvs_handle_t,Handle> handles;uint32_t handleSerial=1;
#ifdef NATIVE_FIXTURE_THREADED
thread_local bool onWorker=false;
std::atomic<bool> lease{false};
#else
bool onWorker=true,lease=true;
#endif
bool initialized=true,armed=false;unsigned nativeCalls=0,cut=0;bool after=false;std::string fault;
unsigned nvsTotal=0,fsTotal=0;unsigned fsCalls=0;uint32_t otherUsed=0,totalBytes=0x80000,nvsCapacity=0x6000,ticks=0;std::string rootPath;
// Optional process-death cutpoints for the import-recovery host fixture.
// Tests persist the simulated NVS state then _exit without C++ cleanup.
void(*powerCheckpoint)(const char*)=nullptr;
void power(const char*at){if(powerCheckpoint)powerCheckpoint(at);}
bool hit(const char*k){if(!armed||fault!=k)return false;return ++nativeCalls==cut;}
bool ioHit(const char*k){if(!armed||fault!=k)return false;return ++fsCalls==cut;}
void safe(){assert(onWorker&&lease);}
uint32_t now(void*){safe();return ticks;}
bool cooperate(void*){safe();++ticks;return true;}
C::Allocator fileAllocation=C::heap();
void* secureAlloc(size_t n){return fileAllocation.allocate(fileAllocation.context,n);}
void secureFree(void*p){fileAllocation.release(fileAllocation.context,p);}
int get(C&c,const char*k,std::string*out){uint8_t bytes[2048]{};uint32_t n=0;int rc=c.get(k,bytes,sizeof bytes,&n);if(!rc)out->assign(reinterpret_cast<char*>(bytes),n);C::wipe(bytes,sizeof bytes);return rc;}
std::string value(C&c,const char*k){std::string out;assert(get(c,k,&out)==C::Ok);return out;}
int put(C&c,const char*k,const char*v){C::Mutation m{k,C::Put,v,uint32_t(strlen(v))};return c.apply(c.revision(),&m,1,"explicit-synthetic-intent",25);}
bool space(void*,uint32_t*total,uint32_t*used){safe();uint64_t n=otherUsed;for(auto&e:std::filesystem::recursive_directory_iterator(rootPath))if(e.is_regular_file())n+=(e.file_size()+4095)/4096*4096;*total=totalBytes;*used=uint32_t(n);return n<=UINT32_MAX;}
struct Fixture {
 RiscStorage::AppDataFiles files{{nullptr,now,cooperate,secureAlloc,secureFree}};
 N metadata{{nullptr,[](void*){return onWorker&&lease;},[](void*){return initialized?ESP_OK:ESP_ERR_NVS_NOT_INITIALIZED;}}};
 RiscExperimental::CheckedFreshInitArtifacts inspector;
 S store;C core;
 explicit Fixture(uint32_t ns=6,C::Allocator allocation=C::heap()):inspector(ns,rootPath.c_str(),{&files,[](void*){return onWorker&&lease;},cooperate,[](void*p){return static_cast<RiscStorage::AppDataFiles*>(p)->retained()?int(C::Retained):int(C::Ok);}}),store(ns,files,metadata.api(),{nullptr,[](void*){return onWorker&&lease;},space},allocation,&inspector),core(ns,store.backend(),S::limits(),allocation){assert(files.configure(rootPath.c_str()));}
};
void reset(){armed=false;onWorker=lease=initialized=true;nativeCalls=fsCalls=cut=0;fault.clear();after=false;otherUsed=0;totalBytes=0x80000;nvsCapacity=0x6000;assert(handles.empty());}
void empty(){reset();nvs.clear();std::filesystem::remove_all(rootPath);std::filesystem::create_directories(rootPath);}
extern "C" esp_err_t nvs_open(const char*name,nvs_open_mode_t mode,nvs_handle_t*out){++nvsTotal;safe();bool fail=hit("nvs-open");if(fail&&!after)return ESP_FAIL;if((strncmp(name,"rnr",3)&&strncmp(name,"rte",3)&&strncmp(name,"kvj",3)&&strncmp(name,"kvd",3))||strlen(name)!=11)return ESP_ERR_INVALID_ARG;if(mode==NVS_READONLY&&!nvs.count(name))return ESP_ERR_NVS_NOT_FOUND;*out=handleSerial++;handles[*out]={name,mode};if(fail&&after){handles.erase(*out);return ESP_FAIL;}return ESP_OK;}
extern "C" esp_err_t nvs_get_blob(nvs_handle_t h,const char*k,void*out,size_t*n){++nvsTotal;safe();bool fail=hit(out?"nvs-read":"nvs-probe");if(fail&&!after)return ESP_FAIL;auto it=handles.find(h);assert(it!=handles.end());auto ns=nvs.find(it->second.name);if(ns==nvs.end()||!ns->second.count(k))return ESP_ERR_NVS_NOT_FOUND;const auto&b=ns->second.at(k);if(!out){*n=b.size();return fail?ESP_FAIL:ESP_OK;}if(*n<b.size()){*n=b.size();return ESP_ERR_NVS_INVALID_LENGTH;}memcpy(out,b.data(),b.size());*n=b.size();return fail?ESP_FAIL:ESP_OK;}
extern "C" esp_err_t nvs_set_blob(nvs_handle_t h,const char*k,const void*p,size_t n){++nvsTotal;safe();bool fail=hit("nvs-set");if(fail&&!after)return ESP_ERR_NVS_NOT_ENOUGH_SPACE;auto it=handles.find(h);assert(it!=handles.end()&&it->second.mode==NVS_READWRITE);assert((!strcmp(k,"root")&&n==48)||(!strcmp(k,"next")&&n==24));size_t used=0;for(auto&space:nvs)for(auto&e:space.second)used+=e.second.size()+64;if(used+n+64>nvsCapacity)return ESP_ERR_NVS_NOT_ENOUGH_SPACE;power("nvs-set-before");nvs[it->second.name][k]=Bytes(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n);power("nvs-set-after");return fail?ESP_FAIL:ESP_OK;}
extern "C" esp_err_t nvs_commit(nvs_handle_t h){++nvsTotal;safe();assert(handles.count(h));power("nvs-commit-before");const auto rc=hit("nvs-commit")?ESP_FAIL:ESP_OK;power("nvs-commit-after");return rc;}
extern "C" void nvs_close(nvs_handle_t h){++nvsTotal;assert(handles.erase(h)==1);}
extern "C" ssize_t __real_write(int,const void*,size_t);
extern "C" ssize_t __wrap_write(int fd,const void*p,size_t n){++fsTotal;safe();if(ioHit("write")){errno=ENOSPC;return -1;}if(ioHit("partial"))return __real_write(fd,p,n/2);power("write-before");if(powerCheckpoint&&n>1){const ssize_t first=__real_write(fd,p,n/2);if(first!=ssize_t(n/2))return first;power("write-prefix");const ssize_t last=__real_write(fd,static_cast<const uint8_t*>(p)+n/2,n-n/2);if(last<0)return last;power("write-after");return first+last;}const auto rc=__real_write(fd,p,n);power("write-after");return rc;}
extern "C" ssize_t __real_read(int,void*,size_t);
extern "C" ssize_t __wrap_read(int fd,void*p,size_t n){++fsTotal;safe();if(ioHit("read")){errno=EIO;return -1;}return __real_read(fd,p,n);}
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd){++fsTotal;safe();if(ioHit("sync")){errno=EIO;return -1;}power("sync-before");const auto rc=__real_fsync(fd);power("sync-after");return rc;}
extern "C" int __real_close(int);
extern "C" int __wrap_close(int fd){++fsTotal;power("close-before");int rc=__real_close(fd);power("close-after");if(ioHit("close")){errno=EIO;return -1;}return rc;}
extern "C" int __real_rename(const char*,const char*);
extern "C" int __wrap_rename(const char*a,const char*b){++fsTotal;safe();if(ioHit("rename-before")){errno=EIO;return -1;}power("rename-before");int rc=__real_rename(a,b);power("rename-after");if(ioHit("rename-after")){errno=EIO;return -1;}return rc;}

} // namespace NativeFixture
