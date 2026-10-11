#include "runtime/packages/StagedAppInstall.h"
#include "runtime/storage/StorageWorker.h"
#include <RiscStorageAsyncAdapterV1.h>
#include <cassert>
#include <map>
#include <string>
#include <cstdio>
using Install=RiscPackages::StagedAppInstall;
static RiscStorage::Worker worker;
static risc_storage_worker_admission_v1 native{};
static risc_storage_volume_api_v1_packages api{};
static risc_storage_async_adapter_v1 adapter{};
static std::map<std::string,std::string> files;
static std::string opened;static size_t offset;
static bool executing,writing,replaceError,replaceApplied,validImage=true,ready=true;
static unsigned validations,mutations;
static void setup(){
 auto&v=api.base.fs.state.prepared.base.sleep.terminal.power.volume.base;
 auto&ext=api.base.fs.state.prepared.base.sleep.terminal.power.volume;
 v={};v.api_version=1;v.struct_size=sizeof(api);
 v.ready=[](void*){assert(executing);return ready;};v.refresh=[](void*){assert(executing);return ready;};
 v.file_open_read=[](void*,const char*p,uint64_t*n)->uint32_t{assert(executing&&opened.empty());auto i=files.find(p);if(i==files.end())return 0;opened=p;offset=0;*n=i->second.size();writing=false;return 1;};
 v.file_read=[](void*,uint32_t,void*out,size_t n)->size_t{assert(executing);auto&s=files[opened];n=std::min(n,s.size()-offset);memcpy(out,s.data()+offset,n);offset+=n;return n;};
 v.file_open_write=[](void*,const char*p)->uint32_t{assert(executing&&opened.empty());if(files.count(p))return 0;opened=p;files[p]="";writing=true;++mutations;return 1;};
 v.file_write=[](void*,uint32_t,const void*p,size_t n)->size_t{assert(executing&&writing);files[opened].append(static_cast<const char*>(p),n);return n;};
 v.file_close=[](void*,uint32_t,bool commit){assert(executing&&!opened.empty());if(writing&&!commit)files.erase(opened);opened.clear();return true;};
 v.remove=[](void*,const char*p){assert(executing);++mutations;return files.erase(p)!=0;};
 ext.file_sync=[](void*,uint32_t){assert(executing);return true;};
 ext.handle_error=[](void*,uint32_t,bool)->uint32_t{return 0;};
 auto&fs=api.base.fs;fs.state.state_tag=RISC_STORAGE_STATE_TAG;fs.state.state_version=1;fs.state.observe=[](void*)->int32_t{return 0;};fs.fs_tag=RISC_STORAGE_FS_TAG;fs.fs_version=1;
 fs.metadata=[](void*,const char*p,risc_storage_metadata_v1*m)->int32_t{assert(executing);m->flags=0;return files.count(p)?RISC_STORAGE_FS_OK:RISC_STORAGE_FS_NOT_FOUND;};
 fs.dir_tell=[](void*,uint32_t,uint64_t*){return false;};fs.dir_seek=[](void*,uint32_t,uint64_t){return false;};fs.file_truncate=[](void*,uint32_t,uint64_t){return false;};
 fs.recover_replace=[](void*,const char*p){assert(executing);std::string backup=std::string(p)+".risc-replace";if(!files.count(p)&&files.count(backup))files[p]=files[backup];files.erase(backup);return true;};
 fs.replace_file=[](void*,const char*src,const char*dst){assert(executing);++mutations;if(!replaceError||replaceApplied){files[dst]=files.at(src);files.erase(src);}return !replaceError;};
 native.base={1,sizeof(native),nullptr,nullptr,
 [](void*,const void*p,uint32_t n,uint64_t*t){return worker.submit(p,n,t);},
 [](void*,uint64_t t,void*p,uint32_t n,uint32_t*s,int32_t*r){return worker.collect(t,p,n,s,r);},
 [](void*,uint64_t t){return worker.cancel(t);},nullptr,nullptr,nullptr,nullptr};
 native.admission_tag=RISC_STORAGE_ADMISSION_TAG;native.admission_version=1;
 native.application=[](void*,const risc_storage_volume_api_v1*,const char*,uint32_t,const uint8_t*,uint32_t*h)->int32_t{assert(executing);*h=0;++validations;return validImage?RISC_STORAGE_WORK_OK:RISC_STORAGE_WORK_INVALID;};
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
