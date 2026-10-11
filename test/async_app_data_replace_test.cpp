#include "runtime/storage/AsyncAppDataReplace.h"
#include "runtime/storage/AppDataFiles.h"
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <string>
#include <cstdio>
#include <unistd.h>
static std::mutex mutex;static std::condition_variable cv;
static bool entered=false,proceed=false;
static int32_t outcome=RISC_APP_DATA_OK;
static std::string captured;
static unsigned char nativeBytes[RISC_APP_DATA_FILE_MAX];
int main(){
 RiscStorage::AsyncAppDataReplace worker(nativeBytes,sizeof(nativeBytes),[](void*,uint32_t ns,const char*name,uint64_t expected,const void*bytes,uint32_t size){
  assert(ns==71&&!strcmp(name,"installed.apps")&&expected==9);
  std::unique_lock<std::mutex>lock(mutex);entered=true;cv.notify_all();cv.wait(lock,[]{return proceed;});
  captured.assign(static_cast<const char*>(bytes),size);return outcome;
 },nullptr);
 std::string payload(RISC_APP_DATA_FILE_MAX,'p');uint64_t token=0;
 assert(worker.begin(71,"installed.apps",9,payload.data(),payload.size(),&token)==0);payload.assign(payload.size(),'x');
 std::thread task([&]{assert(worker.workerStep());});{std::unique_lock<std::mutex>lock(mutex);cv.wait(lock,[]{return entered;});}
 int32_t result=42;for(unsigned i=0;i<10000;++i){assert(worker.pending());assert(worker.collect(token,&result)==RISC_APP_DATA_BUSY&&result==42);}
 uint64_t next=4;assert(worker.begin(71,"installed.apps",9,"bad",3,&next)==RISC_APP_DATA_BUSY&&!next);
 {std::lock_guard<std::mutex>lock(mutex);proceed=true;cv.notify_all();}task.join();
 assert(worker.pending()&&worker.collect(token,&result)==0&&result==0&&!worker.pending());
 assert(captured==std::string(RISC_APP_DATA_FILE_MAX,'p'));assert(worker.collect(token,&result)==RISC_APP_DATA_CONTEXT);
 for(int32_t failure:{RISC_APP_DATA_STALE,RISC_APP_DATA_NO_SPACE,RISC_APP_DATA_COMMIT_UNKNOWN,RISC_APP_DATA_RETAINED}){
  outcome=failure;assert(worker.begin(71,"installed.apps",9,"copy",4,&next)==0&&next!=token);assert(worker.workerStep());assert(worker.collect(next,&result)==0&&result==failure);token=next;
 }
 puts("Async app-data: copied 64KiB payload, 10000 pending polls, stale tokens, exact failure/ambiguous/retained results PASS");
}
