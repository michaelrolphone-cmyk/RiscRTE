#pragma once
#include "native_replica_sd_fixture.h"
#include "experimental/native_replica/CheckedSdSettingsCatalog.h"
#include <set>
using K=RiscExperimental::CheckedSdSettingsCatalog;
struct DirectorySd:Sd {
 std::set<std::string> dirs{"/"};std::vector<risc_storage_dirent_v1> dirEntries;std::string directory;size_t cursor=0;
 bool dirError=false,reverse=false,dots=false,maskOpen=false,maskMetadata=false,sessionUnavailable=false;
 unsigned opens=0,dirCloses=0;int malformed=0;bool injectOnSecond=false;
 bool(*oldRecover)(void*,const char*)=nullptr;
 uint32_t(*oldError)(void*,uint32_t,bool)=nullptr;
 static DirectorySd&self(void*p){return *static_cast<DirectorySd*>(p);}
 void allowed(const char*p){assert(std::string(p)=="/"||std::string(p)=="/RTECFG"||std::string(p)=="/RTECFG/00000006"||std::string(p).find("/RTECFG/00000006/")==0);}
 explicit DirectorySd(Files&f):Sd(f){
  for(const auto&item:f){auto slash=item.first.rfind('/');for(auto parent=item.first.substr(0,slash);!parent.empty();parent=parent.substr(0,parent.rfind('/')))dirs.insert(parent);}
  auto&fs=download.base.base.fs;auto&ext=fs.state.prepared.base.sleep.terminal.power.volume;auto&v=ext.base;
  v.dir_open=[](void*p,const char*path)->uint32_t{auto&s=self(p);s.allowed(path);bool fail=s.point("dir-open");if(fail&&!s.after){s.finish(true);return 0;}if((s.maskOpen&&std::string(path)!="/")||!s.dirs.count(path)){s.finish(fail);return 0;}assert(s.directory.empty());++s.opens;s.directory=path;s.cursor=0;s.dirError=false;s.dirEntries.clear();std::map<std::string,risc_storage_dirent_v1> entries;
   auto add=[&](const std::string&name,uint64_t size,bool dir){const std::string prefix=s.directory=="/"?"/":s.directory+"/";if(name.rfind(prefix,0)||name.size()<=prefix.size())return;const auto tail=name.substr(prefix.size());if(tail.find('/')!=std::string::npos)return;risc_storage_dirent_v1 e{};strncpy(e.name,tail.c_str(),sizeof(e.name)-1);e.size=size;e.is_directory=dir;entries[tail]=e;};
   if(s.injectOnSecond&&s.opens==2)s.files["/RTECFG/00000006/late-artifact"]={3};
   for(const auto&d:s.dirs){add(d,0,true);}for(const auto&file:s.files)add(file.first,file.second.size(),false);for(const auto&e:entries)s.dirEntries.push_back(e.second);
   if(s.reverse&&s.opens%2==0)std::reverse(s.dirEntries.begin(),s.dirEntries.end());
   if(s.dots){risc_storage_dirent_v1 dot{};strcpy(dot.name,".");dot.is_directory=1;s.dirEntries.insert(s.dirEntries.begin(),dot);strcpy(dot.name,"..");s.dirEntries.insert(s.dirEntries.begin(),dot);}
   if(s.malformed){risc_storage_dirent_v1 e{};strcpy(e.name,"bad");if(s.malformed==1)memset(e.name,'x',128);if(s.malformed==2)strcpy(e.name,"a/b");if(s.malformed==3)e.name[0]=0;if(s.malformed==4)e.is_directory=2;s.dirEntries.push_back(e);if(s.malformed==5)s.dirEntries.push_back(e);}
   if(s.finish(fail)){s.directory.clear();return 0;}return 2;};
  v.dir_next=[](void*p,uint32_t h,risc_storage_dirent_v1*out){auto&s=self(p);assert(h==2&&!s.directory.empty());bool fail=s.point("dir-next");if(fail&&!s.after){s.finish(true);s.dirError=true;return false;}if(s.cursor==s.dirEntries.size()){if(s.finish(fail))s.dirError=true;return false;}*out=s.dirEntries[s.cursor++];if(s.finish(fail)){s.dirError=true;return false;}return true;};
  ext.dir_close_checked=[](void*p,uint32_t h){auto&s=self(p);assert(h==2&&!s.directory.empty());bool fail=s.point("dir-close");++s.dirCloses;if(fail&&!s.after){s.finish(true);s.retained=true;return false;}s.directory.clear();if(s.finish(fail)){s.retained=true;return false;}return true;};
  oldError=ext.handle_error;ext.handle_error=[](void*p,uint32_t h,bool directory)->uint32_t{auto&s=self(p);if(!directory)return s.oldError(p,h,false);bool fail=s.point("dir-error");return s.finish(fail)||s.dirError?1:0;};
  oldRecover=fs.recover_replace;fs.recover_replace=[](void*p,const char*path){auto&s=self(p);assert(s.directory.empty());assert(std::string(path)=="/RTECFG/00000006/replica.index"||std::string(path)=="/RTECFG/00000006/settings.index"||std::string(path)=="/RTECFG/00000006/settings.kv");return s.oldRecover(p,path);};
  fs.metadata=[](void*p,const char*path,risc_storage_metadata_v1*m)->int32_t{auto&s=self(p);s.allowed(path);bool fail=s.point("metadata");if(s.finish(fail))return RISC_STORAGE_FS_IO;if(s.maskMetadata&&std::string(path)!="/")return RISC_STORAGE_FS_NOT_FOUND;m->flags=s.dirs.count(path)?RISC_STORAGE_DIRECTORY:0;m->size=s.files.count(path)?s.files.at(path).size():0;return s.dirs.count(path)||s.files.count(path)?RISC_STORAGE_FS_OK:RISC_STORAGE_FS_NOT_FOUND;};
  fs.state.observe=[](void*p){auto&s=self(p);if(s.sessionUnavailable&&s.adapter.session)return int(RISC_STORAGE_STATE_UNAVAILABLE);return s.retained?int(RISC_STORAGE_STATE_RETAINED):s.available?int(RISC_STORAGE_STATE_READY):int(RISC_STORAGE_STATE_UNAVAILABLE);};
 }
};
static uint32_t catalogCrc(const uint8_t*p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
static Bytes legacyImage(bool v1=false){Bytes b(v1?212:184);memcpy(b.data(),v1?"RISCKV1":"RISCKV2",8);put32(b.data()+8,6);put32(b.data()+16,7);strcpy(reinterpret_cast<char*>(b.data()+24),"shared-00000006");put32(b.data()+120,1);if(!v1)put32(b.data()+124,b.size());unsigned at=v1?124:128;strcpy(reinterpret_cast<char*>(b.data()+at),"legacy");put32(b.data()+at+16,32);memset(b.data()+at+20,'v',32);put32(b.data()+b.size()-4,catalogCrc(b.data(),b.size()-4));return b;}
static void addLegacy(Files&f,bool single=false){Bytes b=legacyImage(single);if(single){f["/RTECFG/00000006/settings.kv"]=b;return;}Bytes i(28);memcpy(i.data(),"RTEKVP2",8);put32(i.data()+8,b.size());put32(i.data()+12,7);put32(i.data()+20,catalogCrc(b.data(),b.size()));put32(i.data()+24,catalogCrc(i.data(),24));f["/RTECFG/00000006/settings.index"]=i;f["/RTECFG/00000006/kv-0000000000000007-00000000"]=b;}
struct CatalogAlloc {
 unsigned allocations=0,failAt=0;size_t live=0,peak=0;std::map<void*,size_t>blocks;
 C::Allocator api(){return {this,[](void*p,size_t n)->void*{auto&a=*static_cast<CatalogAlloc*>(p);if(++a.allocations==a.failAt)return nullptr;void*out=malloc(n);if(out){memset(out,0xa7,n);a.blocks[out]=n;a.live+=n;a.peak=std::max(a.peak,a.live);}return out;},[](void*p,void*out){auto&a=*static_cast<CatalogAlloc*>(p);size_t n=a.blocks.at(out);for(size_t i=0;i<n;++i)assert(!static_cast<uint8_t*>(out)[i]);a.blocks.erase(out);a.live-=n;free(out);}};}
 ~CatalogAlloc(){assert(blocks.empty()&&!live);}
};
