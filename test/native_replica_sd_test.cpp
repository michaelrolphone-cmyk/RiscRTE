#include "experimental/native_replica/CheckedSdReplicaCheckpoint.h"
#include "runtime/storage/StorageWorker.h"
#include <RiscStorageAsyncAdapterV1.h>
#include <RiscProtectedSettingsPath.h>
#include <openssl/evp.h>
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
using C=RiscExperimental::NativeReplicaCore;
using T=RiscExperimental::CheckedSdReplicaCheckpoint;
using Bytes=std::vector<uint8_t>;
using Files=std::map<std::string,Bytes>;
static const char*Index="/RTECFG/00000006/replica.index";
static void put32(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(i*8));}
struct Native {
 Bytes root;std::map<uint64_t,Bytes> objects;uint64_t next=1;unsigned calls=0;
 bool failAck=false,applyAck=false,retainedSeal=false;
 C::Backend api(){return {this,
  [](void*v,void*out,uint32_t n){auto&d=*static_cast<Native*>(v);++d.calls;if(d.root.empty())return int(C::Missing);assert(n==d.root.size());memcpy(out,d.root.data(),n);return int(C::Ok);},
  [](void*v,uint32_t n,uint64_t*out){auto&d=*static_cast<Native*>(v);++d.calls;*out=d.next++;d.objects[*out]=Bytes(n);return int(C::Ok);},
  [](void*v,uint64_t id,uint32_t at,const void*p,uint32_t n){auto&d=*static_cast<Native*>(v);++d.calls;assert(at+n<=d.objects.at(id).size());memcpy(d.objects.at(id).data()+at,p,n);return int(C::Ok);},
  [](void*v,uint64_t id,uint32_t at,void*p,uint32_t n){auto&d=*static_cast<Native*>(v);++d.calls;assert(at+n<=d.objects.at(id).size());memcpy(p,d.objects.at(id).data()+at,n);return int(C::Ok);},
  [](void*v,uint64_t){return int(static_cast<Native*>(v)->retainedSeal?C::Retained:C::Ok);},
  [](void*v,const void*p,uint32_t n){auto&d=*static_cast<Native*>(v);++d.calls;if(!d.failAck||d.applyAck)d.root.assign((const uint8_t*)p,(const uint8_t*)p+n);return int(d.failAck?C::Io:C::Ok);},
  [](void*v,uint64_t id){static_cast<Native*>(v)->objects.erase(id);}};}
};
struct Hash {
 EVP_MD_CTX*ctx=EVP_MD_CTX_new();bool broken=false;
 ~Hash(){EVP_MD_CTX_free(ctx);}
 T::Digest api(){return {this,
  [](void*v){auto&h=*static_cast<Hash*>(v);return !h.broken&&EVP_DigestInit_ex(h.ctx,EVP_sha256(),nullptr)==1;},
  [](void*v,const void*p,uint32_t n){assert(n<=4096);auto&h=*static_cast<Hash*>(v);return !h.broken&&EVP_DigestUpdate(h.ctx,p,n)==1;},
  [](void*v,uint8_t*p){auto&h=*static_cast<Hash*>(v);unsigned n=0;return !h.broken&&EVP_DigestFinal_ex(h.ctx,p,&n)==1&&n==32;}};}
};
struct Sd {
 struct Cut{};
 Files&files;RiscStorage::Worker worker;risc_storage_worker_v1 native{};
 risc_storage_volume_api_v1_download download{};risc_storage_async_adapter_v1 adapter{};
 bool executing=false,nativeWorker=true,lease=true,available=true,writing=false,retained=false,terminalBusy=false;
 std::string opened;size_t offset=0;unsigned io=0,mutations=0,ends=0,indexReplacements=0;
 unsigned fault=0;bool crash=false,after=false,hit=false;std::vector<std::string> operations;
 static Sd&self(void*p){return *static_cast<Sd*>(p);}
 bool point(const char*op){assert(executing);++io;operations.emplace_back(op);bool match=io==fault;if(match){hit=true;if(crash&&!after)throw Cut{};}return match;}
 bool finish(bool fail){if(fail&&crash)throw Cut{};return fail;}
 void path(const char*p){assert(risc_protected_settings_path(p));assert(std::string(p).find("/RTECFG/00000006/")==0);}
 explicit Sd(Files&f):files(f){
  auto&pkg=download.base;auto&async=pkg.base;auto&fs=async.fs;auto&ext=fs.state.prepared.base.sleep.terminal.power.volume;auto&v=ext.base;
  v.api_version=1;v.struct_size=sizeof(download);v.context=this;
  v.ready=[](void*p){auto&s=self(p);bool fail=s.point("ready");return !s.finish(fail)&&s.available;};
  v.refresh=[](void*p){auto&s=self(p);bool fail=s.point("refresh");return !s.finish(fail)&&s.available;};
  v.file_open_read=[](void*p,const char*path,uint64_t*n)->uint32_t{auto&s=self(p);s.path(path);bool fail=s.point("open-read");if(fail&&!s.after){s.finish(true);return 0;}assert(s.opened.empty());auto i=s.files.find(path);if(i==s.files.end())return 0;s.opened=path;s.offset=0;s.writing=false;*n=i->second.size();if(s.finish(fail)){s.opened.clear();return 0;}return 1;};
  v.file_read=[](void*p,uint32_t,void*out,size_t n)->size_t{auto&s=self(p);bool fail=s.point("read");if(fail&&!s.after){s.finish(true);return 0;}auto&b=s.files.at(s.opened);n=std::min(n,b.size()-s.offset);memcpy(out,b.data()+s.offset,n);s.offset+=n;return s.finish(fail)?0:n;};
  v.file_open_write=[](void*p,const char*path)->uint32_t{auto&s=self(p);s.path(path);bool fail=s.point("open-write");if(fail&&!s.after){s.finish(true);return 0;}assert(s.opened.empty());if(s.files.count(path))return 0;s.files[path]={};++s.mutations;s.opened=path;s.offset=0;s.writing=true;if(s.finish(fail)){s.opened.clear();return 0;}return 1;};
  v.file_write=[](void*p,uint32_t,const void*data,size_t n)->size_t{auto&s=self(p);bool fail=s.point("write");size_t wrote=fail&&!s.after?n/2:n;auto&b=s.files.at(s.opened);b.insert(b.end(),(const uint8_t*)data,(const uint8_t*)data+wrote);return s.finish(fail)?wrote==n?0:wrote:wrote;};
  ext.file_sync=[](void*p,uint32_t){auto&s=self(p);return !s.finish(s.point("sync"));};
  v.file_close=[](void*p,uint32_t,bool commit){auto&s=self(p);bool fail=s.point("close");if(fail&&!s.after){s.finish(true);s.retained=true;return false;}assert(!s.opened.empty());if(s.writing&&!commit)s.files.erase(s.opened);s.opened.clear();s.writing=false;if(s.finish(fail)){s.retained=true;return false;}return true;};
  v.remove=[](void*p,const char*path){auto&s=self(p);s.path(path);bool fail=s.point("remove");if(fail&&!s.after){s.finish(true);return false;}++s.mutations;bool erased=s.files.erase(path)>0;return !s.finish(fail)&&erased;};
  ext.handle_error=[](void*p,uint32_t,bool)->uint32_t{auto&s=self(p);return s.finish(s.point("handle-error"))?1:0;};
  fs.state.state_tag=RISC_STORAGE_STATE_TAG;fs.state.state_version=1;
  fs.state.observe=[](void*p)->int32_t{auto&s=self(p);return s.retained?RISC_STORAGE_STATE_RETAINED:s.available?RISC_STORAGE_STATE_READY:RISC_STORAGE_STATE_UNAVAILABLE;};
  fs.fs_tag=RISC_STORAGE_FS_TAG;fs.fs_version=1;
  fs.metadata=[](void*p,const char*path,risc_storage_metadata_v1*m)->int32_t{auto&s=self(p);s.path(path);bool fail=s.point("metadata");m->flags=0;return s.finish(fail)?RISC_STORAGE_FS_IO:s.files.count(path)?RISC_STORAGE_FS_OK:RISC_STORAGE_FS_NOT_FOUND;};
  fs.dir_tell=[](void*,uint32_t,uint64_t*){return false;};fs.dir_seek=[](void*,uint32_t,uint64_t){return false;};fs.file_truncate=[](void*,uint32_t,uint64_t){return false;};
  fs.recover_replace=[](void*p,const char*path){auto&s=self(p);s.path(path);bool fail=s.point("recover");if(fail&&!s.after){s.finish(true);return false;}std::string backup=std::string(path)+".risc-replace";if(!s.files.count(path)&&s.files.count(backup))s.files[path]=s.files[backup];s.files.erase(backup);return !s.finish(fail);};
  fs.replace_file=[](void*p,const char*src,const char*dst){auto&s=self(p);s.path(src);s.path(dst);const std::string backup=std::string(dst)+".risc-replace";
   bool fail=s.point("replace-backup");if(fail&&!s.after){s.finish(true);return false;}assert(s.files.count(src));if(s.files.count(dst)){s.files[backup]=s.files.at(dst);s.files.erase(dst);}++s.mutations;if(s.finish(fail))return false;
   fail=s.point("replace-publish");if(fail&&!s.after){s.finish(true);return false;}s.files[dst]=s.files.at(src);s.files.erase(src);++s.indexReplacements;if(s.finish(fail))return false;
   fail=s.point("replace-cleanup");if(fail&&!s.after){s.finish(true);return false;}s.files.erase(backup);return !s.finish(fail);};
  native={1,sizeof(native),this,
   nullptr,[](void*p,const void*q,uint32_t n,uint64_t*t){return self(p).worker.submit(q,n,t);},
   [](void*p,uint64_t t,void*q,uint32_t n,uint32_t*z,int32_t*r){return self(p).worker.collect(t,q,n,z,r);},
   [](void*p,uint64_t t){return self(p).worker.cancel(t);},nullptr,nullptr,nullptr,nullptr};
  adapter.worker=&native;adapter.volume=&v;adapter.ext=&ext;adapter.fs=&fs;assert(worker.bind(risc_storage_async_execute,&adapter)==0);
  async.async_tag=RISC_STORAGE_ASYNC_TAG;async.async_version=3;
  async.begin=[](void*p,uint64_t*t){return risc_storage_async_begin(&self(p).adapter,t);};
  async.submit=[](void*p,uint64_t session,const risc_storage_async_request_v1*q,uint64_t*t){return risc_storage_async_submit(&self(p).adapter,session,q,t);};
  async.collect=[](void*p,uint64_t session,uint64_t op,risc_storage_async_result_v1*r){auto&s=self(p);int rc=risc_storage_async_collect(&s.adapter,session,op,r);if(s.terminalBusy&&rc==0){s.terminalBusy=false;r->status=RISC_STORAGE_ASYNC_BUSY;return int(RISC_STORAGE_ASYNC_BUSY);}return rc;};
  async.end=[](void*p,uint64_t session){auto&s=self(p);++s.ends;return risc_storage_async_end(&s.adapter,session);};
  pkg.package_tag=RISC_STORAGE_PACKAGE_TAG;pkg.package_version=1;pkg.submit_package=[](void*p,uint64_t session,const risc_storage_package_request_v1*q,uint64_t*t){return risc_storage_package_submit(&self(p).adapter,session,q,t);};
  download.download_tag=RISC_STORAGE_DOWNLOAD_TAG;download.download_version=1;
 }
 const risc_storage_volume_api_v1*volume(){return adapter.volume;}
 T::Guard guard(){return {this,[](void*p){return self(p).lease;},[](void*p){return self(p).nativeWorker;}};}
 void tick(T&t,bool run=true){const unsigned before=io;t.step();assert(io==before);if(run){executing=true;try{worker.workerStep();}catch(...){executing=false;throw;}executing=false;}}
 unsigned run(T&t){unsigned n=0;while(t.state()==T::State::Running){tick(t);assert(++n<1000);}return n;}
};
static C::Limits limits(){return {65512,4096,1024};}
static std::string get(C&c,const char*k){char p[2048];uint32_t n=0;assert(c.get(k,p,sizeof p,&n)==0);return {p,n};}
static void edit(C&c,const char*value="local"){C::Mutation m{"edited",C::Put,value,uint32_t(strlen(value))};assert(c.apply(c.revision(),&m,1,"explicit-fake-intent",20)==0);}
static Bytes mapRows(const std::vector<std::string>&keys){Bytes b;for(auto&k:keys){auto at=b.size();b.resize(at+52);memcpy(b.data()+at,k.c_str(),k.size());put32(b.data()+at+16,32);memset(b.data()+at+20,'v',32);}return b;}
static C::Identity identity(const Bytes&b,unsigned count,unsigned gen=7){C::Identity id{};id.known=true;id.generation=gen;id.lineage[0]=17;Bytes input(12);put32(input.data(),6);put32(input.data()+4,count);put32(input.data()+8,b.size());input.insert(input.end(),b.begin(),b.end());unsigned n=0;assert(EVP_Digest(input.data(),input.size(),id.digest,&n,EVP_sha256(),nullptr)==1&&n==32);return id;}
static std::string pagePath(const C::Identity&id,unsigned page){char out[160],digest[65];for(unsigned i=0;i<32;++i)snprintf(digest+2*i,3,"%02x",id.digest[i]);snprintf(out,sizeof out,"/RTECFG/00000006/nr-%08x%08x-%s-%08x",unsigned(id.generation>>32),unsigned(id.generation),digest,page);return out;}
static void save(Files&f,const Bytes&b,unsigned count,C::Identity id){Bytes idx(T::IndexBytes);T::makeIndex(idx.data(),6,b.size(),count,id);f[Index]=idx;for(size_t at=0;at<b.size();at+=4096)f[pagePath(id,at/4096)]=Bytes(b.begin()+at,b.begin()+std::min(at+4096,b.size()));}
static void seed(Native&native,Files&files,unsigned count=100){std::vector<std::string> keys;std::vector<C::Value> rows;for(unsigned i=0;i<count;++i){char k[16];snprintf(k,sizeof k,"k%05u",i);keys.emplace_back(k);}for(auto&k:keys)rows.push_back({k.c_str(),"vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv",32});Bytes b=mapRows(keys);auto id=identity(b,count);save(files,b,count,id);C core(6,native.api(),limits(),C::heap());assert(core.initialize(rows.data(),rows.size(),id)==0);edit(core);}
static void unchanged(Native&n){C core(6,n.api(),limits(),C::heap());assert(core.load()==0);assert(get(core,"edited")=="local"&&core.pendingCount()==1);assert(get(core,"k00000")==std::string(32,'v'));}
static void retry(Native&n,Files&files){C core(6,n.api(),limits(),C::heap());assert(core.load()==0);Sd sd(files);Hash h;T t(h.api(),sd.guard());assert(t.start(core,sd.volume())==0);sd.run(t);if(t.state()!=T::State::Verified){fprintf(stderr,"retry failure state=%d err=%d files=%zu\n",int(t.state()),t.error(),files.size());for(auto&op:sd.operations)fprintf(stderr,"%s ",op.c_str());fprintf(stderr,"\n");}assert(t.state()==T::State::Verified);assert(t.acknowledgeNative()==0);assert(core.pendingCount()==0&&get(core,"edited")=="local");}
static void basics(){
 Native n;Files files;seed(n,files,1025);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd sd(files);Hash h;T t(h.api(),sd.guard());assert(t.start(c,sd.volume())==0);
 for(unsigned i=0;i<1000;++i){sd.tick(t,false);}assert(sd.io==0&&c.pendingCount()==1);
 edit(c,"later");sd.run(t);assert(t.state()==T::State::Verified&&sd.ends&&c.pendingCount()==2&&!t.exitSafe());
 sd.nativeWorker=false;assert(t.acknowledgeNative()==C::Busy);sd.nativeWorker=true;assert(t.acknowledgeNative()==0&&t.exitSafe());assert(c.pendingCount()==1&&get(c,"edited")=="later"&&get(c,"k01024")==std::string(32,'v'));
 C reboot(6,n.api(),limits(),C::heap());assert(reboot.load()==0&&reboot.pendingCount()==1&&get(reboot,"edited")=="later");
 puts("SD transport: 1025 opaque keys, immutable complete snapshot, exact captured frontier, newer edit survives, checked close before worker-only acknowledgement PASS");
}
static void identityGates(){
 for(unsigned mode=0;mode<10;++mode){Native n;Files files;seed(n,files);const Files original=files;C c(6,n.api(),limits(),C::heap());assert(c.load()==0);
  auto id=c.identity();Bytes idx=files[Index];if(mode==0)files.clear();if(mode==1)files[Index][0]^=1;
  if(mode==2){id.lineage[0]^=1;T::makeIndex(files[Index].data(),6,5200,100,id);}
  if(mode==3){--id.generation;T::makeIndex(files[Index].data(),6,5200,100,id);}
  if(mode==4){id.digest[0]^=1;T::makeIndex(files[Index].data(),6,5200,100,id);}
  if(mode==5){++id.generation;T::makeIndex(files[Index].data(),6,5200,100,id);}
  if(mode==6)files.at(pagePath(id,0))[30]^=1;
  if(mode==7)files.erase(pagePath(id,1));
  Sd sd(files);if(mode==8)sd.available=false;if(mode==9)sd.lease=false;Hash h;T t(h.api(),sd.guard());
  const int rc=t.start(c,sd.volume());if(rc==0){sd.run(t);assert(t.state()==T::State::Failed);}
  else assert(mode==9&&rc==C::Conflict&&t.reason()==T::Reason::CustodyLost);
  if(mode==0)assert(t.reason()==T::Reason::CheckpointMissing);
  if(mode==8)assert(t.reason()==T::Reason::MediaUnavailable);
  if(mode>=2&&mode<=5)assert(t.reason()==T::Reason::IdentityConflict);
  assert(sd.mutations==0&&get(c,"edited")=="local"&&c.pendingCount()==1);unchanged(n);
 }
 Native n;Files f;C unknown(6,n.api(),limits(),C::heap());assert(unknown.initialize(nullptr,0,{})==0);edit(unknown);Sd sd(f);Hash h;T t(h.api(),sd.guard());assert(t.start(unknown,sd.volume())==C::Conflict&&sd.io==0&&unknown.pendingCount()==1);
 puts("SD transport: absent/not-ready/unreadable/foreign/older/newer/divergent/corrupt SD cannot change native reads or consume intents; unknown base refuses all SD work PASS");
}
static void interruptionMatrix(){
 Native initial;Files original;seed(initial,original);unsigned steps=0;
 {Native n=initial;Files f=original;C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;T t(h.api(),s.guard());assert(t.start(c,s.volume())==0);steps=s.run(t);assert(t.state()==T::State::Verified);}
 unsigned cases=0;
 // Stop on every owner/worker boundary, both before and after copied execution.
 for(bool queued:{false,true})for(unsigned mode=0;mode<4;++mode)for(unsigned cut=0;cut<=steps;++cut){Native n=initial;Files f=original;
  {C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;T t(h.api(),s.guard());assert(t.start(c,s.volume())==0);
   for(unsigned i=0;i<cut&&t.state()==T::State::Running;++i)s.tick(t);
   if(queued&&t.state()==T::State::Running)s.tick(t,false);
   if(mode==0){t.cancel();s.run(t);assert(t.state()==T::State::Cancelled&&t.exitSafe());}
   if(mode==1){s.available=false;t.cancel();s.run(t);assert(t.state()==T::State::Cancelled&&t.exitSafe());}
   if(mode==2){s.lease=false;if(t.state()==T::State::Verified)assert(t.acknowledgeNative()==C::Conflict);else s.run(t);assert(t.state()==T::State::Failed&&t.exitSafe());}
   // mode 3 discards all process/worker state, retaining only durable model files.
   assert(c.pendingCount()==1&&get(c,"edited")=="local");
  }
  unchanged(n);retry(n,f);++cases;
 }
 printf("SD transport: %u cancel/removal/USB-lease revocation/restart boundaries, exact replay and no pending-edit loss PASS\n",cases);
}
static void faults(){
 Native initial;Files original;seed(initial,original);unsigned io=0;
 {Native n=initial;Files f=original;C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;T t(h.api(),s.guard());assert(t.start(c,s.volume())==0);s.run(t);assert(t.state()==T::State::Verified);io=s.io;}
 unsigned cases=0;
 for(bool crash:{false,true})for(bool after:{false,true})for(unsigned cut=1;cut<=io;++cut){Native n=initial;Files f=original;
  {C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);s.fault=cut;s.after=after;s.crash=crash;Hash h;T t(h.api(),s.guard());assert(t.start(c,s.volume())==0);
   try{s.run(t);}catch(const Sd::Cut&){}
   assert(s.hit&&c.pendingCount()==1&&get(c,"edited")=="local");
   if(t.state()==T::State::Retained){unsigned before=s.io;t.cancel();for(unsigned i=0;i<10;++i)s.tick(t,false);assert(!t.exitSafe()&&s.io==before&&t.acknowledgeNative()==C::Stale);}
  }
  unchanged(n);retry(n,f);++cases;
 }
 printf("SD transport: %u reached before/after returned-error/power-cut filesystem calls, torn pages, checked read/sync/close/replace and exact restart replay PASS\n",cases);
}
static void restartAndRaces(){
 for(unsigned mode=0;mode<7;++mode){Native n;Files f;seed(n,f);
  {C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;T t(h.api(),s.guard());assert(t.start(c,s.volume())==0);s.run(t);assert(t.state()==T::State::Verified);
   if(mode==0){s.available=false;assert(t.acknowledgeNative()==C::Io);}
   if(mode==1){s.retained=true;assert(t.acknowledgeNative()==C::Retained&&!t.exitSafe());}
   if(mode==2){assert(c.load()==0);assert(t.acknowledgeNative()==C::Stale);}
   if(mode==3){edit(c,"after-restart");}
   if(mode==6){n.retainedSeal=true;assert(t.acknowledgeNative()==C::Retained);t.cancel();assert(t.state()==T::State::Retained&&!t.exitSafe());n.retainedSeal=false;}
   if(mode==4||mode==5){n.failAck=true;n.applyAck=mode==5;assert(t.acknowledgeNative()==C::Uncertain);n.failAck=false;}
  }
  C reboot(6,n.api(),limits(),C::heap());assert(reboot.load()==0);
  if(mode==3){Sd s(f);Hash h;T t(h.api(),s.guard());assert(t.start(reboot,s.volume())==0);s.run(t);assert(t.state()==T::State::Failed&&t.error()==C::Conflict&&reboot.pendingCount()==2&&get(reboot,"edited")=="after-restart"&&s.mutations==0);}
  else if(mode==5)assert(!reboot.pendingCount());else {assert(reboot.pendingCount()==1);retry(n,f);}
 }
 // Tamper the index after pages are staged, before the final compare/replace.
 Native n;Files f;seed(n,f);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;T t(h.api(),s.guard());assert(t.start(c,s.volume())==0);
 while(!f.count("/RTECFG/00000006/.replica.next")){s.tick(t);assert(t.state()==T::State::Running);}
 f[Index][32]^=1;s.run(t);assert(t.state()==T::State::Failed&&t.error()==C::Conflict&&!s.indexReplacements&&c.pendingCount()==1);
 puts("SD transport: expired tickets, late removal/retention, ambiguous native acknowledgement, restart with newer edits and pre-replacement race fail closed PASS");
}
static void recoveryAndBounds(){
 for(bool foreign:{false,true}){Native n;Files f;seed(n,f);if(foreign){auto bytes=Bytes(5200,'x');auto id=identity(bytes,100);id.lineage[0]=91;T::makeIndex(f[Index].data(),6,5200,100,id);}f[std::string(Index)+".risc-replace"]=f.at(Index);f.erase(Index);
  C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;T t(h.api(),s.guard());assert(t.start(c,s.volume())==0);s.run(t);
  if(foreign)assert(t.state()==T::State::Failed&&t.reason()==T::Reason::IdentityConflict&&!s.indexReplacements&&c.pendingCount()==1);
  else {assert(t.state()==T::State::Verified);assert(t.acknowledgeNative()==0);}
 }
 // Empty maps have no zero-byte page writes. DELETE is still explicit intent.
 Native n;Files f;auto id=identity({},0);save(f,{},0,id);C c(6,n.api(),limits(),C::heap());assert(c.initialize(nullptr,0,id)==0);C::Mutation deletion{"absent",C::Delete,nullptr,0};assert(c.apply(c.revision(),&deletion,1,"delete",6)==0);Sd sd(f);Hash h;T t(h.api(),sd.guard());assert(t.start(c,sd.volume())==0);sd.run(t);assert(t.state()==T::State::Verified&&t.acknowledgeNative()==0&&!c.pendingCount());
 // Allocation failure does not replace an already-live capture ticket.
 struct Alloc{bool fail=false;C::Allocator api(){return {this,[](void*p,size_t n)->void*{return static_cast<Alloc*>(p)->fail?nullptr:malloc(n);},[](void*,void*p){free(p);}};}} alloc;
 Native n2;Files f2;seed(n2,f2);C c2(6,n2.api(),limits(),alloc.api());assert(c2.load()==0);C::Ticket ticket;assert(c2.capture(&ticket)==0);alloc.fail=true;C::Snapshot snapshot;assert(c2.captureSnapshot(&snapshot)==C::NoMemory&&!snapshot.valid());alloc.fail=false;auto next=c2.identity();++next.generation;assert(c2.acknowledge(ticket,next)==0);
 // A completed BUSY result has consumed its operation token; never poll it.
 Native n3;Files f3;seed(n3,f3);C c3(6,n3.api(),limits(),C::heap());assert(c3.load()==0);Sd busy(f3);busy.terminalBusy=true;Hash h3;T t3(h3.api(),busy.guard());assert(t3.start(c3,busy.volume())==0);busy.run(t3);assert(t3.state()==T::State::Failed&&c3.pendingCount()==1&&t3.exitSafe());retry(n3,f3);
 // A selected page corrupted after index publication is never acknowledged or
 // silently repaired as if it were an unselected staging orphan.
 Native n4;Files f4;seed(n4,f4);C c4(6,n4.api(),limits(),C::heap());assert(c4.load()==0);Sd bad(f4);Hash h4;T t4(h4.api(),bad.guard());assert(t4.start(c4,bad.volume())==0);while(!bad.indexReplacements){bad.tick(t4);assert(t4.state()==T::State::Running);}C::Identity selected{};selected.known=true;selected.generation=8;memcpy(selected.digest,f4[Index].data()+48,32);f4.at(pagePath(selected,0))[0]^=1;bad.run(t4);assert(t4.state()==T::State::Failed&&c4.pendingCount()==1&&get(c4,"edited")=="local");
 puts("SD transport: interrupted replacement backup recovery, foreign recovered base refusal, empty-map tombstone and snapshot-allocation failure PASS");
}
int main(){setbuf(stdout,nullptr);basics();identityGates();interruptionMatrix();faults();restartAndRaces();recoveryAndBounds();puts("DISCONNECTED HOST TRANSPORT ONLY: no port, ABI, migration, .90 routing, Wi-Fi resolver or hardware qualification");}
