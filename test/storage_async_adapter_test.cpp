#include "runtime/storage/StorageWorker.h"
#include <RiscStorageAsyncAdapterV1.h>
#include <cassert>
#include <cstdio>
#include <thread>
#include <mutex>
#include <condition_variable>
static RiscStorage::Worker worker;
static risc_storage_worker_v1 wapi{};
static risc_storage_volume_api_v1_fs volume{};
static risc_storage_async_adapter_v1 adapter{};
static bool dir,file,present=true,readError,retainedClose,delayed,entered,proceed;
static unsigned closes,dircloses;
static std::mutex mutex;static std::condition_variable cv;
static uint64_t session,operation;
static risc_storage_async_result_v1 result;
static void submit(unsigned op,const char*path="/Apps"){
 risc_storage_async_request_v1 q{};q.operation=op;strcpy(q.path,path);
 assert(risc_storage_async_submit(&adapter,session,&q,&operation)==0);
 memset(&q,0xff,sizeof(q)); // executor must not borrow the caller's request
}
static int32_t collect(){return risc_storage_async_collect(&adapter,session,operation,&result);}
int main(){
 wapi={};wapi.api_version=1;wapi.struct_size=sizeof(wapi);
 wapi.submit=[](void*,const void*p,uint32_t n,uint64_t*t){return worker.submit(p,n,t);};
 wapi.collect=[](void*,uint64_t t,void*p,uint32_t n,uint32_t*s,int32_t*r){return worker.collect(t,p,n,s,r);};
 wapi.cancel=[](void*,uint64_t t){return worker.cancel(t);};
 auto&ext=*reinterpret_cast<risc_storage_volume_api_v1_ext*>(&volume);auto&v=ext.base;
 v.ready=[](void*){return present;};v.refresh=[](void*){return true;};
 v.dir_open=[](void*,const char*p)->uint32_t{assert(!strcmp(p,"/Apps"));dir=true;return 1;};
 ext.dir_close_checked=[](void*,uint32_t){assert(dir);++dircloses;dir=false;return true;};
 v.file_open_read=[](void*,const char*,uint64_t*n)->uint32_t{assert(!file);file=true;*n=4;return 2;};
 v.file_read=[](void*,uint32_t,void*p,size_t)->size_t{
  if(delayed){std::unique_lock<std::mutex>lock(mutex);entered=true;cv.notify_all();cv.wait(lock,[](){return proceed;});}
  if(readError||!present)return 0;memcpy(p,"DATA",4);return 4;
 };
 v.file_close=[](void*,uint32_t,bool){assert(file);++closes;if(retainedClose)return false;file=false;return true;};
 ext.handle_error=[](void*,uint32_t,bool)->uint32_t{return 0;};
 adapter.worker=&wapi;adapter.volume=&v;adapter.ext=&ext;adapter.fs=&volume;
 assert(worker.bind(risc_storage_async_execute,&adapter)==0);
 assert(risc_storage_async_begin(&adapter,&session)==0);submit(RISC_STORAGE_ASYNC_OPEN_DIRECTORY);assert(worker.workerStep());assert(collect()==0&&dir);
 delayed=true;submit(RISC_STORAGE_ASYNC_READ_SMALL_FILE,"/Apps/new/manifest.json");
 std::thread task([](){assert(worker.workerStep());});{std::unique_lock<std::mutex>lock(mutex);cv.wait(lock,[](){return entered;});}
 for(unsigned frames=0;frames<1000;++frames)assert(collect()==RISC_STORAGE_ASYNC_PENDING);
 assert(risc_storage_async_end(&adapter,session)==RISC_STORAGE_ASYNC_PENDING);assert(worker.unbind()==RISC_STORAGE_WORK_BUSY);
 {std::lock_guard<std::mutex>lock(mutex);present=false;proceed=true;cv.notify_all();}task.join();
 assert(risc_storage_async_end(&adapter,session)==RISC_STORAGE_ASYNC_PENDING); // queues checked dir cleanup
 for(unsigned frames=0;frames<1000;++frames)assert(risc_storage_async_end(&adapter,session)==RISC_STORAGE_ASYNC_PENDING);
 assert(worker.workerStep());assert(risc_storage_async_end(&adapter,session)==0&&!dir&&!file&&closes==1&&dircloses==1);
 present=true;delayed=false;assert(risc_storage_async_begin(&adapter,&session)==0);
 submit(RISC_STORAGE_ASYNC_READ_SMALL_FILE);assert(worker.workerStep());assert(collect()==0&&result.size==4&&!memcmp(result.data,"DATA",4));assert(risc_storage_async_end(&adapter,session)==0);
 // Version-three idempotent removal executes only on the worker and refuses
 // directories. Older version-two discovery remains prefix compatible.
 bool exists=true,isDirectory=false;unsigned removals=0;
 struct Removal {bool*exists;bool*directory;unsigned*count;} removal{&exists,&isDirectory,&removals};
 v.context=&removal;
 volume.metadata=[](void*c,const char*,risc_storage_metadata_v1*m)->int32_t{auto*r=static_cast<Removal*>(c);m->flags=*r->directory?RISC_STORAGE_DIRECTORY:0;return *r->exists?RISC_STORAGE_FS_OK:RISC_STORAGE_FS_NOT_FOUND;};
 v.remove=[](void*c,const char*){auto*r=static_cast<Removal*>(c);++*r->count;*r->exists=false;return true;};
 assert(risc_storage_async_begin(&adapter,&session)==0);
 submit(RISC_STORAGE_ASYNC_REMOVE_FILE);assert(removals==0);assert(worker.workerStep());assert(collect()==0&&removals==1&&!exists);
 submit(RISC_STORAGE_ASYNC_REMOVE_FILE);assert(worker.workerStep());assert(collect()==0&&removals==1);
 exists=isDirectory=true;submit(RISC_STORAGE_ASYNC_REMOVE_FILE);assert(worker.workerStep());assert(collect()==RISC_STORAGE_ASYNC_IO&&removals==1);
 assert(risc_storage_async_end(&adapter,session)==0);
 assert(risc_storage_async_begin(&adapter,&session)==0);retainedClose=true;
 submit(RISC_STORAGE_ASYNC_READ_SMALL_FILE);assert(worker.workerStep());assert(collect()==RISC_STORAGE_ASYNC_RETAINED);
 const unsigned before=closes;assert(risc_storage_async_end(&adapter,session)==RISC_STORAGE_ASYNC_RETAINED&&closes==before&&file);
 puts("async volume: delayed storage without owner wait, copied request, removal, cancel/drain, no cleanup starvation, repeated session, retained close PASS");
}
