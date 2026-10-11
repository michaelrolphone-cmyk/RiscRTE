#include "runtime/packages/StagedAppInstall.h"
#include "runtime/packages/SdPackageDownload.h"
#include <set>
#include "runtime/storage/StorageWorker.h"
#include <RiscStorageAsyncAdapterV1.h>
#include <cassert>
#include <map>
#include <string>
#include <cstdio>
using Install=RiscPackages::StagedAppInstall;
static RiscStorage::Worker worker;
static risc_storage_worker_admission_v2 native2{};
#define native native2.base
static risc_storage_volume_api_v1_download downloadApi{};
#define api downloadApi.base
static std::set<std::string> directories;
static risc_storage_async_adapter_v1 adapter{};
static std::map<std::string,std::string> files;
static std::string opened;static size_t offset;
static bool executing,writing,replaceError,replaceApplied,validImage=true,ready=true;
static unsigned validations,mutations;
static void setup(){
 auto&v=api.base.fs.state.prepared.base.sleep.terminal.power.volume.base;
 auto&ext=api.base.fs.state.prepared.base.sleep.terminal.power.volume;
 v={};v.api_version=1;v.struct_size=sizeof(downloadApi);
 v.ready=[](void*){assert(executing);return ready;};v.refresh=[](void*){assert(executing);return ready;};
 v.file_open_read=[](void*,const char*p,uint64_t*n)->uint32_t{assert(executing&&opened.empty());auto i=files.find(p);if(i==files.end())return 0;opened=p;offset=0;*n=i->second.size();writing=false;return 1;};
 v.file_read=[](void*,uint32_t,void*out,size_t n)->size_t{assert(executing);auto&s=files[opened];n=std::min(n,s.size()-offset);memcpy(out,s.data()+offset,n);offset+=n;return n;};
 v.file_open_write=[](void*,const char*p)->uint32_t{assert(executing&&opened.empty());if(files.count(p))return 0;opened=p;files[p]="";writing=true;++mutations;return 1;};
 v.file_write=[](void*,uint32_t,const void*p,size_t n)->size_t{assert(executing&&writing);files[opened].append(static_cast<const char*>(p),n);return n;};
 v.file_close=[](void*,uint32_t,bool commit){assert(executing&&!opened.empty());if(writing&&!commit)files.erase(opened);opened.clear();return true;};
 v.remove=[](void*,const char*p){assert(executing);++mutations;return files.erase(p)!=0;};
 ext.file_sync=[](void*,uint32_t){assert(executing);return true;};
 ext.mkdir=[](void*,const char*p){assert(executing);++mutations;directories.insert(p);return true;};
 ext.handle_error=[](void*,uint32_t,bool)->uint32_t{return 0;};
 auto&fs=api.base.fs;fs.state.state_tag=RISC_STORAGE_STATE_TAG;fs.state.state_version=1;fs.state.observe=[](void*)->int32_t{return 0;};fs.fs_tag=RISC_STORAGE_FS_TAG;fs.fs_version=1;
 fs.metadata=[](void*,const char*p,risc_storage_metadata_v1*m)->int32_t{assert(executing);m->flags=directories.count(p)?RISC_STORAGE_DIRECTORY:0;return (files.count(p)||directories.count(p))?RISC_STORAGE_FS_OK:RISC_STORAGE_FS_NOT_FOUND;};
 fs.dir_tell=[](void*,uint32_t,uint64_t*){return false;};fs.dir_seek=[](void*,uint32_t,uint64_t){return false;};fs.file_truncate=[](void*,uint32_t,uint64_t){return false;};
 fs.recover_replace=[](void*,const char*p){assert(executing);std::string backup=std::string(p)+".risc-replace";if(!files.count(p)&&files.count(backup))files[p]=files[backup];files.erase(backup);return true;};
 fs.replace_file=[](void*,const char*src,const char*dst){assert(executing);++mutations;if(!replaceError||replaceApplied){files[dst]=files.at(src);files.erase(src);}return !replaceError;};
 native.base={1,sizeof(native2),nullptr,nullptr,
 [](void*,const void*p,uint32_t n,uint64_t*t){return worker.submit(p,n,t);},
 [](void*,uint64_t t,void*p,uint32_t n,uint32_t*s,int32_t*r){return worker.collect(t,p,n,s,r);},
 [](void*,uint64_t t){return worker.cancel(t);},nullptr,nullptr,nullptr,nullptr};
 native.admission_tag=RISC_STORAGE_ADMISSION_TAG;native.admission_version=1;
 native.application=[](void*,const risc_storage_volume_api_v1*,const char*,uint32_t,const uint8_t*,uint32_t*h)->int32_t{assert(executing);*h=0;++validations;return validImage?RISC_STORAGE_WORK_OK:RISC_STORAGE_WORK_INVALID;};
 native2.driver_tag=RISC_STORAGE_DRIVER_ADMISSION_TAG;native2.driver_version=1;
 native2.driver=[](void*,const risc_storage_volume_api_v1*,const char*p,uint32_t n,const uint8_t*,uint32_t*h)->int32_t{assert(executing);*h=0;++validations;return validImage&&files.count(p)&&files[p].size()==n?RISC_STORAGE_WORK_OK:RISC_STORAGE_WORK_INVALID;};
 downloadApi.download_tag=RISC_STORAGE_DOWNLOAD_TAG;downloadApi.download_version=1;
 adapter.worker=&native.base;adapter.volume=&v;adapter.ext=&ext;adapter.fs=&fs;assert(worker.bind(risc_storage_async_execute,&adapter)==0);
 api.base.async_tag=RISC_STORAGE_ASYNC_TAG;api.base.async_version=3;
 api.base.begin=[](void*,uint64_t*t){return risc_storage_async_begin(&adapter,t);};
 api.base.submit=[](void*,uint64_t s,const risc_storage_async_request_v1*q,uint64_t*t){return risc_storage_async_submit(&adapter,s,q,t);};
 api.base.collect=[](void*,uint64_t s,uint64_t t,risc_storage_async_result_v1*r){return risc_storage_async_collect(&adapter,s,t,r);};
 api.base.end=[](void*,uint64_t s){return risc_storage_async_end(&adapter,s);};
 api.package_tag=RISC_STORAGE_PACKAGE_TAG;api.package_version=1;api.submit_package=[](void*,uint64_t s,const risc_storage_package_request_v1*q,uint64_t*t){return risc_storage_package_submit(&adapter,s,q,t);};
}
static void tick(Install&i){auto before=mutations;i.step();assert(mutations==before);executing=true;worker.workerStep();executing=false;}
static bool terminal(Install::State s){return s==Install::State::Ready||s==Install::State::Failed||s==Install::State::Cancelled;}
int main(){setup();auto*volume=adapter.volume;
 using Download=RiscPackages::SdPackageDownload;
 static std::string source(7011,'d');static size_t httpAt;static bool httpLive;static unsigned httpPoll;
 risc_http_client_v1 http{1,sizeof(http),nullptr,
 [](void*,const risc_http_request_v1*q,uint64_t*h)->int32_t{assert(!httpLive&&q->max_bytes==source.size());httpLive=true;httpAt=0;httpPoll=0;*h=1;return RISC_HTTP_OK;},
 [](void*,uint64_t,void*out,uint32_t cap,uint32_t*n)->int32_t{assert(httpLive);*n=0;if(++httpPoll%3)return RISC_HTTP_AGAIN;if(httpAt==source.size())return RISC_HTTP_EOF;*n=uint32_t(std::min(size_t(cap),source.size()-httpAt));memcpy(out,source.data()+httpAt,*n);httpAt+=*n;return RISC_HTTP_OK;},
 [](void*,uint64_t,risc_http_response_v1*r)->int32_t{r->status_code=200;r->received_bytes=uint32_t(httpAt);r->content_length=source.size();return RISC_HTTP_OK;},
 [](void*,uint64_t)->int32_t{assert(httpLive);httpLive=false;return RISC_HTTP_OK;}};
 for(unsigned cut=0;cut<110;++cut){
  files.clear();directories.clear();Download::Record record{};strcpy(record.url,"https://example.org/provider.elf");
  strcpy(record.path,"/Drivers/new/image-0000000000000000000000000000000000000000000000000000000000000000.elf");record.size=uint32_t(source.size());record.driver=true;
  Download d;assert(d.start(volume,&http,record,1800000000));auto before=mutations;
  for(unsigned poll=0;poll<1000;++poll)d.step();assert(mutations==before&&!httpLive);assert(d.journalCommitted());
  auto tickDownload=[&](Download&x){auto prior=mutations;x.step();assert(prior==mutations);executing=true;worker.workerStep();executing=false;};
  for(unsigned step=0;step<cut;++step)tickDownload(d);
  d.cancel();unsigned spins=0;while(d.state()!=Download::State::Cancelled&&d.state()!=Download::State::Ready){assert(d.state()!=Download::State::Retained&&d.state()!=Download::State::Failed);tickDownload(d);assert(++spins<100);}
  assert(!httpLive&&opened.empty());Download resumed;assert(resumed.start(volume,&http,record,1800000000,true));spins=0;
  while(resumed.state()!=Download::State::Ready){assert(resumed.state()!=Download::State::Failed&&resumed.state()!=Download::State::Retained);tickDownload(resumed);assert(++spins<400);}
  assert(files.at(record.path)==source&&!httpLive&&opened.empty());
 }
 puts("SD download: real HTTP/copy-worker coordinator, 110 interruption/resume boundaries, journal gate and exact size PASS");
 files.clear();directories.clear();
 // The actual copied worker writes large images in bounded chunks. A metadata
 // request cannot replace its live write handle; cancellation closes on worker.
 for(bool cancel:{false,true}){
  uint64_t session=0,operation=0;assert(api.base.begin(nullptr,&session)==0);
  auto send=[&](uint32_t op,uint32_t n){risc_storage_package_request_v1 q{};q.operation=op;q.size=n;strcpy(q.path,"/download.part");memset(q.data,'x',sizeof(q.data));
   assert(api.submit_package(nullptr,session,&q,&operation)==0);assert(!executing);
   risc_storage_async_result_v1 result{};
   for(unsigned frame=0;frame<1000;++frame)assert(api.base.collect(nullptr,session,operation,&result)==RISC_STORAGE_ASYNC_PENDING);
   executing=true;assert(worker.workerStep());executing=false;
   return api.base.collect(nullptr,session,operation,&result);};
  assert(send(RISC_STORAGE_PACKAGE_OPEN_WRITE,0)==0);
  assert(send(RISC_STORAGE_PACKAGE_WRITE_CHUNK,4096)==0);
  assert(send(RISC_STORAGE_PACKAGE_WRITE_CHUNK,903)==0);
  risc_storage_async_request_v1 read{};read.operation=RISC_STORAGE_ASYNC_READ_SMALL_FILE;strcpy(read.path,"/other");
  assert(api.base.submit(nullptr,session,&read,&operation)==0);executing=true;worker.workerStep();executing=false;
  risc_storage_async_result_v1 result{};assert(api.base.collect(nullptr,session,operation,&result)==RISC_STORAGE_ASYNC_BUSY&&opened=="/download.part");
  if(cancel){assert(api.base.end(nullptr,session)==RISC_STORAGE_ASYNC_PENDING);executing=true;worker.workerStep();executing=false;}
  else assert(send(RISC_STORAGE_PACKAGE_CLOSE_WRITE,4999)==0);
  assert(api.base.end(nullptr,session)==0&&opened.empty());
  assert(files.count("/download.part")==unsigned(!cancel));files.clear();
 }

 for(bool existing:{false,true})for(unsigned cut=0;cut<24;++cut){
  files.clear();if(existing)files["/Apps/new/manifest.json"]="old";
  Install::Record r{};strcpy(r.manifestPath,"/Apps/new/manifest.json");strcpy(r.stagePath,"/Apps/new/manifest.install");strcpy(r.imagePath,"/Apps/new/image.elf");strcpy(r.manifest,"new");r.imageSize=100;
  Install i;assert(i.start(volume,r,false));bool journal=false;unsigned steps=0;
  while(!terminal(i.state())){
   if(i.state()==Install::State::Journal){assert(files.count(r.manifestPath)==unsigned(existing));if(existing)assert(files[r.manifestPath]=="old");journal=true;assert(i.journalCommitted());}
   tick(i);if(++steps==cut){i.cancel();while(!terminal(i.state()))tick(i);break;}assert(steps<100);
  }
  if(journal){Install recovered;assert(recovered.start(volume,r,true));unsigned recoverySteps=0;while(!terminal(recovered.state())){tick(recovered);assert(++recoverySteps<100);}assert(recovered.state()==Install::State::Ready&&files[r.manifestPath]=="new");}
  else assert(files.count(r.manifestPath)==unsigned(existing)&&(!existing||files[r.manifestPath]=="old"));
  assert(!adapter.session&&opened.empty());
 }
 for(bool applied:{false,true}){
  Install::Record r{};strcpy(r.manifestPath,"/Apps/new/manifest.json");strcpy(r.stagePath,"/Apps/new/manifest.install");strcpy(r.imagePath,"/Apps/new/image.elf");strcpy(r.manifest,"next");r.imageSize=100;
  files[r.manifestPath]="old";replaceError=true;replaceApplied=applied;Install i;assert(i.start(volume,r,false));unsigned n=0;
  while(!terminal(i.state())){if(i.state()==Install::State::Journal)assert(i.journalCommitted());tick(i);assert(++n<100);}
  assert(i.state()==(applied?Install::State::Ready:Install::State::Failed));replaceError=false;
 }
 puts("Staged install: 48 interruption boundaries, durable journal before mutation, replay, ambiguous replacement reconciliation, owner-only polling PASS");
}
