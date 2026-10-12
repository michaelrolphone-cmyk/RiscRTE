#pragma once
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
[[maybe_unused]] static const char*Index="/RTECFG/00000006/replica.index";
[[maybe_unused]] static void put32(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(i*8));}
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
 std::string opened,directory;std::vector<risc_storage_dirent_v1> entries;size_t cursor=0;bool dirError=false;size_t offset=0;unsigned io=0,mutations=0,ends=0,indexReplacements=0;
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
  v.dir_open=[](void*p,const char*path)->uint32_t{auto&s=self(p);assert(std::string(path)=="/RTECFG/00000006");bool fail=s.point("dir-open");if(fail&&!s.after){s.finish(true);return 0;}assert(s.directory.empty());s.directory=path;s.entries.clear();s.cursor=0;s.dirError=false;const std::string prefix=s.directory+"/";for(const auto&file:s.files){if(file.first.rfind(prefix,0))continue;const auto leaf=file.first.substr(prefix.size());assert(leaf.find('/')==std::string::npos&&leaf.size()<128);risc_storage_dirent_v1 e{};strcpy(e.name,leaf.c_str());e.size=file.second.size();s.entries.push_back(e);}if(s.finish(fail)){s.directory.clear();return 0;}return 2;};
  v.dir_next=[](void*p,uint32_t h,risc_storage_dirent_v1*out){auto&s=self(p);assert(h==2&&!s.directory.empty());bool fail=s.point("dir-next");if(fail&&!s.after){s.finish(true);s.dirError=true;return false;}if(s.cursor==s.entries.size()){if(s.finish(fail))s.dirError=true;return false;}*out=s.entries[s.cursor++];if(s.finish(fail)){s.dirError=true;return false;}return true;};
  ext.dir_close_checked=[](void*p,uint32_t h){auto&s=self(p);assert(h==2&&!s.directory.empty());bool fail=s.point("dir-close");if(fail&&!s.after){s.finish(true);s.retained=true;return false;}s.directory.clear();if(s.finish(fail)){s.retained=true;return false;}return true;};
  ext.file_sync=[](void*p,uint32_t){auto&s=self(p);return !s.finish(s.point("sync"));};
  v.file_close=[](void*p,uint32_t,bool commit){auto&s=self(p);bool fail=s.point("close");if(fail&&!s.after){s.finish(true);s.retained=true;return false;}assert(!s.opened.empty());if(s.writing&&!commit)s.files.erase(s.opened);s.opened.clear();s.writing=false;if(s.finish(fail)){s.retained=true;return false;}return true;};
  v.remove=[](void*p,const char*path){auto&s=self(p);s.path(path);bool fail=s.point("remove");if(fail&&!s.after){s.finish(true);return false;}++s.mutations;bool erased=s.files.erase(path)>0;return !s.finish(fail)&&erased;};
  ext.handle_error=[](void*p,uint32_t,bool directory)->uint32_t{auto&s=self(p);return s.finish(s.point("handle-error"))||(directory&&s.dirError)?1:0;};
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
 template<class Job> void tick(Job&t,bool run=true){const unsigned before=io;t.step();assert(io==before);if(run){executing=true;try{worker.workerStep();}catch(...){executing=false;throw;}executing=false;}}
 template<class Job> unsigned run(Job&t){unsigned n=0;while(t.state()==Job::State::Running){tick(t);assert(++n<100000);}return n;}
};
[[maybe_unused]] static C::Limits limits(){return {65512,4096,1024};}
[[maybe_unused]] static std::string get(C&c,const char*k){char p[2048];uint32_t n=0;assert(c.get(k,p,sizeof p,&n)==0);return {p,n};}
[[maybe_unused]] static void edit(C&c,const char*value="local"){C::Mutation m{"edited",C::Put,value,uint32_t(strlen(value))};assert(c.apply(c.revision(),&m,1,"explicit-fake-intent",20)==0);}
[[maybe_unused]] static Bytes mapRows(const std::vector<std::string>&keys){Bytes b;for(auto&k:keys){auto at=b.size();b.resize(at+52);memcpy(b.data()+at,k.c_str(),k.size());put32(b.data()+at+16,32);memset(b.data()+at+20,'v',32);}return b;}
[[maybe_unused]] static C::Identity identity(const Bytes&b,unsigned count,unsigned gen=7){C::Identity id{};id.known=true;id.generation=gen;id.lineage[0]=17;Bytes input(12);put32(input.data(),6);put32(input.data()+4,count);put32(input.data()+8,b.size());input.insert(input.end(),b.begin(),b.end());unsigned n=0;assert(EVP_Digest(input.data(),input.size(),id.digest,&n,EVP_sha256(),nullptr)==1&&n==32);return id;}
[[maybe_unused]] static std::string pagePath(const C::Identity&id,unsigned page){char out[160],digest[65];for(unsigned i=0;i<32;++i)snprintf(digest+2*i,3,"%02x",id.digest[i]);snprintf(out,sizeof out,"/RTECFG/00000006/nr-%08x%08x-%s-%08x",unsigned(id.generation>>32),unsigned(id.generation),digest,page);return out;}
[[maybe_unused]] static void save(Files&f,const Bytes&b,unsigned count,C::Identity id){Bytes idx(T::IndexBytes);T::makeIndex(idx.data(),6,b.size(),count,id);f[Index]=idx;for(size_t at=0;at<b.size();at+=4096)f[pagePath(id,at/4096)]=Bytes(b.begin()+at,b.begin()+std::min(at+4096,b.size()));}
[[maybe_unused]] static void seed(Native&native,Files&files,unsigned count=100){std::vector<std::string> keys;std::vector<C::Value> rows;for(unsigned i=0;i<count;++i){char k[16];snprintf(k,sizeof k,"k%05u",i);keys.emplace_back(k);}for(auto&k:keys)rows.push_back({k.c_str(),"vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv",32});Bytes b=mapRows(keys);auto id=identity(b,count);save(files,b,count,id);C core(6,native.api(),limits(),C::heap());assert(core.initialize(rows.data(),rows.size(),id)==0);edit(core);}
[[maybe_unused]] static void unchanged(Native&n){C core(6,n.api(),limits(),C::heap());assert(core.load()==0);assert(get(core,"edited")=="local"&&core.pendingCount()==1);assert(get(core,"k00000")==std::string(32,'v'));}
[[maybe_unused]] static void retry(Native&n,Files&files){C core(6,n.api(),limits(),C::heap());assert(core.load()==0);Sd sd(files);Hash h;T t(h.api(),sd.guard());assert(t.start(core,sd.volume())==0);sd.run(t);if(t.state()!=T::State::Verified){fprintf(stderr,"retry failure state=%d err=%d files=%zu\n",int(t.state()),t.error(),files.size());for(auto&op:sd.operations)fprintf(stderr,"%s ",op.c_str());fprintf(stderr,"\n");}assert(t.state()==T::State::Verified);assert(t.acknowledgeNative()==0);assert(core.pendingCount()==0&&get(core,"edited")=="local");}
