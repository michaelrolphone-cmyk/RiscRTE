#include "runtime/storage/StorageWorker.h"
#include <cassert>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <cstdio>
struct Fixture {
 std::mutex mutex;std::condition_variable cv;bool entered=false,proceed=false;unsigned executions=0;
 static int32_t run(void*c,const void*in,uint32_t size,void*out,uint32_t cap,uint32_t*n){
  auto&f=*static_cast<Fixture*>(c);assert(cap>=size);
  {std::unique_lock<std::mutex> lock(f.mutex);f.entered=true;f.cv.notify_all();f.cv.wait(lock,[&](){return f.proceed;});}
  ++f.executions;memcpy(out,in,size);*n=size;return 37;
 }
 void wait(){std::unique_lock<std::mutex>lock(mutex);cv.wait(lock,[&](){return entered;});}
 void release(){std::lock_guard<std::mutex>lock(mutex);proceed=true;cv.notify_all();}
};
int main(){
 RiscStorage::Worker w;Fixture f;assert(w.bind(Fixture::run,&f)==0);
 char request[]="copied before dispatch",result[64]{};uint64_t token=0;uint32_t size=0;int32_t status=0;
 assert(w.submit(request,sizeof(request),&token)==0);const uint64_t first=token;
 memset(request,'X',sizeof(request));std::thread worker([&](){assert(w.workerStep());});f.wait();
 // A deliberately stalled storage operation leaves owner/UI work responsive.
 for(unsigned frames=0;frames<10000;++frames)assert(w.collect(token,result,sizeof(result),&size,&status)==RISC_STORAGE_WORK_PENDING);
 assert(w.unbind()==RISC_STORAGE_WORK_BUSY);assert(w.cancel(token)==RISC_STORAGE_WORK_PENDING);
 assert(w.unbind()==RISC_STORAGE_WORK_BUSY);f.release();worker.join();
 assert(w.cancel(token)==RISC_STORAGE_WORK_CANCELLED);assert(w.idle());
 assert(w.collect(first,result,sizeof(result),&size,&status)==RISC_STORAGE_WORK_INVALID);
 for(unsigned i=0;i<1000;++i){
  strcpy(request,"next generation");assert(w.submit(request,16,&token)==0&&token>first);
  if(i%2){assert(w.cancel(token)==RISC_STORAGE_WORK_CANCELLED);assert(!w.workerStep());continue;}
  assert(w.workerStep());assert(w.collect(token,result,1,&size,&status)==RISC_STORAGE_WORK_INVALID&&size==16);
  assert(w.collect(token,result,sizeof(result),&size,&status)==0&&status==37&&!strcmp(result,"next generation"));
 }
 assert(w.unbind()==0&&!w.bound());assert(f.executions==501);
 puts("storage worker: copied requests/results, delayed I/O, cancellation, pin-until-quiescent, stale tokens, 1000 generations PASS");
}
