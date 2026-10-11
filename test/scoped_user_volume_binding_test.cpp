#include "runtime/storage/ScopedUserVolumeBinding.h"
#include "fixtures/scoped_volume_provider_control.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <functional>
extern "C" {
const risc_storage_volume_api_v1* scoped_fatfs_create();
bool scoped_fatfs_safe(void*);
void scoped_fatfs_fail_write(uint32_t);
uint32_t scoped_fatfs_writes();
void scoped_fatfs_destroy();
bool scoped_browser_copy(const risc_storage_volume_api_v1*,const char*,const char*);
bool scoped_browser_copy_safe();
bool scoped_browser_retry_close();
}
namespace {
struct Control {
 bool owned=true,nativeSafe=true,startOkay=true;
 bool revokeOnStart=false,revokeOnQuiesce=false;
 unsigned starts=0,quiesces=0,stops=0,refuseQuiesce=0;
 std::function<void()> probe;
 scoped_volume_provider_control api{};
 explicit Control(const risc_storage_volume_api_v1_ext*volume):api{1,sizeof(api),this,volume,
  [](void*c){auto&s=*static_cast<Control*>(c);++s.starts;if(s.probe)s.probe();if(s.revokeOnStart)s.nativeSafe=false;return s.startOkay;},
  [](void*c){auto&s=*static_cast<Control*>(c);++s.quiesces;if(s.probe)s.probe();if(s.revokeOnQuiesce)s.nativeSafe=false;if(s.refuseQuiesce){--s.refuseQuiesce;return false;}return true;},
  [](void*c){++static_cast<Control*>(c)->stops;}}{}
 static bool owner(void*c){return static_cast<Control*>(c)->owned;}
 static bool safe(void*c){return static_cast<Control*>(c)->nativeSafe && scoped_fatfs_safe(nullptr);}
 RiscStorage::ScopedUserVolume::Hooks hooks(){return {this,owner,safe};}
};
void seed(const risc_storage_volume_api_v1*v,const char*p,const std::string&value){
 auto h=v->file_open_write(v->context,p);assert(h);
 for(size_t offset=0;offset<value.size();){auto n=v->file_write(v->context,h,value.data()+offset,value.size()-offset);assert(n);offset+=n;}
 assert(v->file_close(v->context,h,true));
}
std::string read(const risc_storage_volume_api_v1*v,const char*p){
 uint64_t size=0;auto h=v->file_open_read(v->context,p,&size);assert(h);std::string data(size,'\0');
 for(size_t offset=0;offset<data.size();){auto n=v->file_read(v->context,h,&data[offset],data.size()-offset);assert(n);offset+=n;}
 assert(v->file_close(v->context,h,true));return data;
}
void terminal(const char*message){puts(message);fflush(stdout);std::_Exit(0);}
}
int main(int argc,char**argv){
 assert(argc==4);const std::string mode=argv[3];
 const auto*provider=scoped_fatfs_create();const auto*ext=risc_storage_volume_extension(provider);
 assert(ext->mkdir(nullptr,"/User") && ext->mkdir(nullptr,"/User/Copies") && ext->mkdir(nullptr,"/Apps") && ext->mkdir(nullptr,"/Private"));
 const std::string payload(5000,'c');seed(provider,"/User/source.txt",payload);seed(provider,"/Apps/default.elf","installed");seed(provider,"/Private/settings.json","settings");
 Control control(ext),other(ext);RuntimeProviders::GraphV2 graph;
 risc_hardware_device_v1 hw1{},hw2{};hw1.api_version=hw2.api_version=1;hw1.struct_size=hw2.struct_size=sizeof(hw1);hw1.instance_id=11;hw2.instance_id=22;
 RuntimeProviders::RequirementV2 req{"fixture.volume",1,nullptr,0,&control.api},otherReq{"fixture.volume",1,nullptr,0,&other.api};
 auto admit=[&]{RuntimeProviders::SpecV2 spec{"fixture-user-storage",argv[1],"storage.volume",1,&req,1};
  if(mode=="positive_instance" || mode=="ambiguous_instance")spec.hardware=&hw1;
  assert(graph.addVerified(spec));
  if(spec.hardware){spec.hardware=&hw2;spec.requirements=&otherReq;assert(graph.addVerified(spec));}
 };
 assert(graph.addVerified({"fixture-other-storage",argv[2],"storage.volume",1,&otherReq,1}));
 if(mode!="missing_provider")admit();
 RiscStorage::ScopedUserVolumeBinding binding(graph,control.hooks());
 for(const char*root:{"/","relative","/../private","/User/..","/~Reserved"})assert(!binding.configure("fixture-user-storage",0,root,"User files"));
 char id[]="fixture-user-storage",root[32]="/User",label[]="User files";
 if(mode=="missing_root")strcpy(root,"/Missing");
 const uint64_t instance=mode=="wrong_instance"?999:mode=="positive_instance"?11:0;
 assert(binding.configure(id,instance,root,label));memset(id,'x',sizeof(id)-1);memset(root,'x',strlen(root));memset(label,'x',sizeof(label)-1);
 risc_storage_volume_api_v1_ext scoped{};scoped.base.api_version=77;
 if(mode=="wrong_instance" || mode=="ambiguous_instance"){
  assert(!binding.begin(&scoped) && scoped.base.api_version==77 && !control.starts && !other.starts && !graph.liveGrants());
  assert(binding.exitSafe() && graph.shutdown());scoped_fatfs_destroy();puts("Exact provider instance refusal PASS");return 0;
 }
 if(mode=="unsafe_admission"){
  control.nativeSafe=false;assert(!binding.begin(&scoped) && !binding.retained() && binding.exitSafe());control.nativeSafe=true;
 }
 risc_storage_volume_api_v1_ext incomplete=*ext;
 if(mode=="missing_operations" || mode=="missing_media"){
  if(mode=="missing_operations")incomplete.mkdir=nullptr;
  else incomplete.base.ready=[](void*){return false;};
  control.api.volume=&incomplete;const auto writes=scoped_fatfs_writes();
  assert(!binding.begin(&scoped) && binding.exitSafe() && scoped.base.api_version==77 && control.stops==1);
  assert(scoped_fatfs_writes()==writes);
  control.api.volume=ext;
 }
 if(mode=="missing_provider"){
  assert(!binding.begin(&scoped) && scoped.base.api_version==77 && !other.starts && binding.exitSafe());admit();
 }
 if(mode=="missing_root"){
  assert(!binding.begin(&scoped) && scoped.base.api_version==77 && control.starts==1 && control.stops==1 && binding.exitSafe());
  assert(ext->mkdir(nullptr,"/Missing"));
 }
 if(mode=="start_failure"){
  control.startOkay=false;assert(!binding.begin(&scoped));assert(!graph.liveGrants());
  if(binding.retained())terminal("Failed activation retained without automatic recovery PASS");
  control.startOkay=true;
 }
 if(mode=="activation_custody")control.revokeOnStart=true;
 if(mode=="failed_activation_retained"){control.startOkay=false;control.refuseQuiesce=100;}
 if(mode=="activation_custody" || mode=="failed_activation_retained"){
  assert(!binding.begin(&scoped) && binding.retained() && !binding.exitSafe() && scoped.base.api_version==77);
  const auto q=control.quiesces,starts=control.starts;control.nativeSafe=true;
  assert(!binding.begin(&scoped) && !binding.end() && control.quiesces==q && control.starts==starts);
  terminal("Failed/revoked activation retains graph without automatic recovery PASS");
 }
 unsigned probes=0;
 if(mode=="reentrant")control.probe=[&]{++probes;risc_storage_volume_api_v1_ext out{};assert(!binding.begin(&out) && !binding.end() && !binding.exitSafe());};
 if(mode=="owner"){
  control.owned=false;assert(!binding.begin(&scoped) && !control.starts);control.owned=true;
 }
 assert(binding.begin(&scoped));assert(!binding.exitSafe() && graph.liveGrants()==1 && !other.starts);
 if(mode=="adapter_capacity"){
  std::optional<RiscStorage::ScopedUserVolumeBinding> extra[4];
  risc_storage_volume_api_v1_ext tables[4]{};
  for(unsigned i=0;i<4;++i){extra[i].emplace(graph,control.hooks());assert(extra[i]->configure("fixture-user-storage",0,"/User","User files"));
   tables[i].base.api_version=77;assert(extra[i]->begin(&tables[i])==(i<3));}
  assert(graph.liveGrants()==4 && extra[3]->exitSafe() && tables[3].base.api_version==77 && !control.quiesces);
  for(unsigned i=0;i<3;++i){assert(tables[i].base.ready(tables[i].base.context));assert(extra[i]->end());}
  assert(graph.liveGrants()==1 && !control.quiesces);
 }
 RiscStorage::ScopedUserVolumeBinding companion(graph,control.hooks());risc_storage_volume_api_v1_ext companionTable{};
 if(mode=="shared_provider"){assert(companion.configure("fixture-user-storage",0,"/User/Copies","Copies"));assert(companion.begin(&companionTable));assert(graph.liveGrants()==2);}
 auto&a=scoped.base;char name[64]{};assert(a.label(a.context,name,sizeof(name)) && !strcmp(name,"User files"));
 if(mode=="missing_root"){
  assert(scoped.mkdir(a.context,"/Nested"));assert(binding.end() && binding.exitSafe());scoped_fatfs_destroy();puts("Missing root clean rollback and explicit retry PASS");return 0;
 }
 uint64_t size=0;bool directory=false;
 assert(!a.stat(a.context,"/../Private/settings.json",&size,&directory));assert(!a.stat(a.context,"/Apps/default.elf",&size,&directory));
 if(mode=="custody"){
  control.nativeSafe=false;assert(!a.ready(a.context));assert(binding.retained());const auto q=control.quiesces;
  assert(!binding.end() && !binding.begin(&scoped) && graph.liveGrants()==1 && control.quiesces==q);
  terminal("Lost storage custody retains binding, mapped provider and grant without cleanup retry PASS");
 }
 if(mode=="full"){
  char bytes[4096]{},freePath[64]{};bool full=false;
  // Bound each later removal too: a single whole-disk file would exceed the
  // production provider's per-operation sector budget during unlink.
  for(unsigned index=0;!full;++index){
   assert(index<1024);char path[64];snprintf(path,sizeof(path),"/User/F%04u.BIN",index);
   auto h=provider->file_open_write(nullptr,path);if(!h){full=true;break;}
   unsigned written=0;for(unsigned block=0;block<64;++block){const auto n=provider->file_write(nullptr,h,bytes,sizeof(bytes));written+=n;if(n<sizeof(bytes)){full=true;break;}}
   assert(provider->file_close(nullptr,h,true));if(written)strcpy(freePath,path);
  }
  assert(!scoped_browser_copy(&a,"/source.txt","/Copies/copy.txt"));
  assert(scoped_browser_copy_safe() && !binding.retained());
  assert(freePath[0] && provider->remove(nullptr,freePath));
 }
 if(mode=="io_failure")scoped_fatfs_fail_write(1);
 const bool copied=scoped_browser_copy(&a,"/source.txt","/Copies/copy.txt");
 if(mode=="io_failure"){
  assert(!copied && binding.retained());const auto q=control.quiesces,writes=scoped_fatfs_writes();
  if(!scoped_browser_copy_safe())assert(!scoped_browser_retry_close());
  assert(!binding.end() && !binding.begin(&scoped) && graph.liveGrants()==1);
  assert(q==control.quiesces && writes==scoped_fatfs_writes());terminal("Native I/O failure retains actual browser, scoped binding and provider graph PASS");
 }
 assert(copied && scoped_browser_copy_safe() && read(&a,"/Copies/copy.txt")==payload && read(&a,"/source.txt")==payload);
 if(mode=="shared_provider")assert(read(&companionTable.base,"/copy.txt")==payload);
 assert(scoped.mkdir(a.context,"/New"));assert(scoped.rename(a.context,"/Copies/copy.txt","/New/moved.txt"));
 assert(a.remove(a.context,"/New/moved.txt"));
 if(mode=="owner"){
  control.owned=false;const auto q=control.quiesces;assert(!binding.end() && !a.ready(a.context) && control.quiesces==q);control.owned=true;
 }
 auto saved=scoped;
 if(mode=="release_retry"){
  control.refuseQuiesce=1;assert(!binding.end());assert(binding.releasePending() && !binding.exitSafe() && !binding.retained());
  assert(!saved.base.ready(saved.base.context) && !binding.begin(&scoped));assert(control.quiesces==1 && !control.stops);
 }
 if(mode=="release_custody"){
  control.revokeOnQuiesce=true;control.refuseQuiesce=1;
  assert(!binding.end() && binding.retained());const auto q=control.quiesces;control.nativeSafe=true;
  assert(!binding.end() && !binding.begin(&scoped) && control.quiesces==q);
  terminal("Native custody loss during provider release remains terminal PASS");
 }
 assert(binding.end() && binding.exitSafe() && !binding.releasePending() && !binding.error()[0]);
 if(mode=="shared_provider"){
  assert(graph.liveGrants()==1 && !control.quiesces && companionTable.base.ready(companionTable.base.context));
  assert(companion.end());
 }
 assert(!graph.liveGrants());
 assert(!saved.base.ready(saved.base.context));assert(control.starts==control.stops && !other.starts);
 assert(read(provider,"/Apps/default.elf")=="installed" && read(provider,"/Private/settings.json")=="settings");
 assert(binding.begin(&scoped) && scoped.base.context!=saved.base.context);assert(!saved.mkdir(saved.base.context,"/stale"));
 assert(binding.end() && graph.shutdown());scoped_fatfs_destroy();
 if(mode=="reentrant")assert(probes==4);
 printf("Real provider graph + scoped binding + actual browser/FatFs %s PASS\n",mode.c_str());
}
